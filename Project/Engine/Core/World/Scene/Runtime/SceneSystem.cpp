#include "SceneSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <atomic>
#include <thread>
#include <utility>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace {

	constexpr uint32_t kSceneSchemaVersion = 3;
	constexpr uint32_t kExternalActorSchemaVersion = 1;

	bool ValidateSceneFileRoot(const nlohmann::json& root) {

		if (!root.is_object()) {
			return false;
		}
		const bool hasExternalActors =
			root.contains("ExternalActors") &&
			root["ExternalActors"].is_array();
		const bool hasEntities =
			root.contains("Entities") &&
			root["Entities"].is_array();
		return root.value("SchemaVersion", 0u) == kSceneSchemaVersion &&
			root.contains("Header") && root["Header"].is_object() &&
			root.contains("PrefabInstances") && root["PrefabInstances"].is_array() &&
			hasExternalActors != hasEntities;
	}

	std::filesystem::path NormalizePath(const std::filesystem::path& path) {

		std::error_code ec;
		const std::filesystem::path normalized =
			std::filesystem::weakly_canonical(path, ec);
		return ec ? path.lexically_normal() : normalized;
	}

	bool IsPathInside(const std::filesystem::path& path,
		const std::filesystem::path& root) {

		const std::filesystem::path normalizedPath = NormalizePath(path);
		const std::filesystem::path normalizedRoot = NormalizePath(root);
		auto pathIt = normalizedPath.begin();
		for (auto rootIt = normalizedRoot.begin(); rootIt != normalizedRoot.end();
			++rootIt, ++pathIt) {

			if (pathIt == normalizedPath.end() ||
				Engine::Algorithm::ToLower(pathIt->string()) !=
				Engine::Algorithm::ToLower(rootIt->string())) {
				return false;
			}
		}
		return true;
	}

	// 外部ゲームのGameAssetsだけを競合しにくいActor分割形式で保存する
	bool ShouldUseExternalActors(
		const std::filesystem::path& scenePath) {

		return Engine::RuntimePaths::GetSceneStorageMode() ==
				Engine::SceneStorageMode::ExternalActors &&
			IsPathInside(scenePath,
				Engine::RuntimePaths::GetGameAssetsRoot());
	}

	std::filesystem::path ResolveExternalActorsRoot(
		const std::filesystem::path& scenePath, Engine::AssetID sceneAsset) {

		if (!sceneAsset) {
			return {};
		}

		std::vector<std::filesystem::path> roots = {
			Engine::RuntimePaths::GetGameAssetsRoot(),
			Engine::RuntimePaths::GetEngineAssetsRoot(),
		};
		for (const Engine::ResolvedPackage& package :
			Engine::RuntimePaths::GetPackages()) {
			roots.emplace_back(package.root);
		}
		for (const std::filesystem::path& root : roots) {

			if (!root.empty() && IsPathInside(scenePath, root)) {
				return root / "ExternalActors" / Engine::ToString(sceneAsset);
			}
		}
		return {};
	}

	void RemoveExternalActors(
		const std::filesystem::path& scenePath,
		Engine::AssetID sceneAsset) {

		const std::filesystem::path actorRoot =
			ResolveExternalActorsRoot(scenePath, sceneAsset);
		if (actorRoot.empty()) {
			return;
		}

		std::error_code ec;
		std::filesystem::remove_all(actorRoot, ec);
		if (ec) {
			Engine::Logger::Output(
				Engine::LogType::Engine, spdlog::level::warn,
				"[SceneSystem] ExternalActorを削除できません path={}",
				Engine::Algorithm::PathToUTF8(actorRoot));
			return;
		}
		std::filesystem::remove(actorRoot.parent_path(), ec);
	}

	std::filesystem::path MakeExternalActorPath(
		const std::filesystem::path& actorRoot, Engine::UUID localFileID) {

		return actorRoot / (Engine::ToString(localFileID) + ".actor.json");
	}

	bool LoadExternalActors(const std::filesystem::path& scenePath,
		Engine::AssetID sceneAsset, nlohmann::json& root) {

		const auto externalActors = root.find("ExternalActors");
		if (externalActors == root.end() || !externalActors->is_array()) {
			return false;
		}

		const std::filesystem::path actorRoot =
			ResolveExternalActorsRoot(scenePath, sceneAsset);
		if (actorRoot.empty()) {
			return false;
		}

		nlohmann::json entities = nlohmann::json::array();
		std::unordered_set<Engine::UUID> actorIDs;
		std::vector<Engine::UUID> localFileIDs;
		std::vector<std::filesystem::path> actorPaths;
		localFileIDs.reserve(externalActors->size());
		actorPaths.reserve(externalActors->size());
		for (const auto& actorReference : *externalActors) {

			if (!actorReference.is_string()) {
				return false;
			}
			const std::optional<Engine::UUID> localFileID =
				Engine::TryParseUUID16Hex(actorReference.get<std::string>());
			if (!localFileID || !actorIDs.insert(*localFileID).second) {
				return false;
			}

			localFileIDs.emplace_back(*localFileID);
			actorPaths.emplace_back(
				MakeExternalActorPath(actorRoot, *localFileID));
		}

		// 小さいExternalActorを固定ワーカーで並列解析し大量シーンの起動待ちを抑える
		std::vector<nlohmann::json> loadedActors(actorPaths.size());
		std::atomic_size_t nextActorIndex = 0;
		std::atomic_size_t failedActorIndex = actorPaths.size();
		const size_t workerCount = (std::min)(
			actorPaths.size(),
			static_cast<size_t>((std::max)(
				1u, std::thread::hardware_concurrency())));
		std::vector<std::thread> workers;
		workers.reserve(workerCount);
		for (size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {

			workers.emplace_back([&]() {
				while (failedActorIndex.load(std::memory_order_relaxed) ==
					actorPaths.size()) {

					const size_t actorIndex =
						nextActorIndex.fetch_add(1, std::memory_order_relaxed);
					if (actorPaths.size() <= actorIndex) {
						return;
					}

					nlohmann::json actor =
						Engine::JsonAdapter::Load(actorPaths[actorIndex]);
					if (!actor.is_object() ||
						actor.value("SchemaVersion", 0u) != kExternalActorSchemaVersion ||
						Engine::FromString16Hex(
							actor.value("LocalFileID", std::string{})) !=
							localFileIDs[actorIndex] ||
						!actor.contains("Components") ||
						!actor["Components"].is_object()) {

						size_t expected = actorPaths.size();
						failedActorIndex.compare_exchange_strong(
							expected, actorIndex, std::memory_order_relaxed);
						return;
					}
					actor.erase("SchemaVersion");
					loadedActors[actorIndex] = std::move(actor);
				}
				});
		}
		for (std::thread& worker : workers) {
			worker.join();
		}

		const size_t failedIndex =
			failedActorIndex.load(std::memory_order_relaxed);
		if (failedIndex != actorPaths.size()) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"[SceneSystem] ExternalActorが存在しないか不正です path={}",
				Engine::Algorithm::PathToUTF8(actorPaths[failedIndex]));
			return false;
		}

		entities.get_ref<nlohmann::json::array_t&>().reserve(
			loadedActors.size());
		for (nlohmann::json& actor : loadedActors) {

			entities.push_back(std::move(actor));
		}
		root["Entities"] = std::move(entities);
		return true;
	}

	bool SaveExternalActors(const std::filesystem::path& scenePath,
		Engine::AssetID sceneAsset, nlohmann::json& root) {

		if (!sceneAsset || !root.contains("Entities") ||
			!root["Entities"].is_array()) {
			return true;
		}

		const std::filesystem::path actorRoot =
			ResolveExternalActorsRoot(scenePath, sceneAsset);
		if (actorRoot.empty()) {
			return false;
		}

		struct ActorSaveEntry {

			std::filesystem::path path;
			nlohmann::json data;
		};

		nlohmann::json actorIDs = nlohmann::json::array();
		std::unordered_set<std::string> actorFileNames;
		std::vector<ActorSaveEntry> actorEntries;
		actorEntries.reserve(root["Entities"].size());
		for (const nlohmann::json& entity : root["Entities"]) {

			const std::optional<Engine::UUID> localFileID =
				Engine::TryParseUUID16Hex(
					entity.value("LocalFileID", std::string{}));
			if (!localFileID) {
				return false;
			}

			nlohmann::json actor = entity;
			actor["SchemaVersion"] = kExternalActorSchemaVersion;
			const std::filesystem::path actorPath =
				MakeExternalActorPath(actorRoot, *localFileID);
			actorIDs.push_back(Engine::ToString(*localFileID));
			actorFileNames.insert(actorPath.filename().string());
			actorEntries.push_back({
				.path = actorPath,
				.data = std::move(actor),
				});
		}

		// ExternalActorは互いに独立しているため固定数のワーカーで正規化と書き込みを進める
		std::atomic_size_t nextActorIndex = 0;
		std::atomic_size_t failedActorIndex = actorEntries.size();
		const size_t hardwareThreads = static_cast<size_t>(
			(std::max)(1u, std::thread::hardware_concurrency()));
		const size_t workerCount = (std::min)(
			actorEntries.size(), (std::min)(hardwareThreads, size_t(8)));
		std::vector<std::thread> workers;
		workers.reserve(workerCount);
		for (size_t workerIndex = 0;
			workerIndex < workerCount; ++workerIndex) {

			workers.emplace_back([&]() {
				while (failedActorIndex.load(
					std::memory_order_relaxed) == actorEntries.size()) {

					const size_t actorIndex = nextActorIndex.fetch_add(
						1, std::memory_order_relaxed);
					if (actorEntries.size() <= actorIndex) {
						return;
					}
					const ActorSaveEntry& entry =
						actorEntries[actorIndex];
					if (!Engine::JsonAdapter::SaveCanonical(
						entry.path, entry.data)) {

						size_t expected = actorEntries.size();
						failedActorIndex.compare_exchange_strong(
							expected, actorIndex,
							std::memory_order_relaxed);
						return;
					}
				}
				});
		}
		for (std::thread& worker : workers) {
			worker.join();
		}

		const size_t failedIndex =
			failedActorIndex.load(std::memory_order_relaxed);
		if (failedIndex != actorEntries.size()) {

			Engine::Logger::Output(
				Engine::LogType::Engine, spdlog::level::err,
				"[SceneSystem] ExternalActorを保存できません path={}",
				Engine::Algorithm::PathToUTF8(
					actorEntries[failedIndex].path));
			return false;
		}

		root.erase("Entities");
		root["ExternalActors"] = std::move(actorIDs);
		if (!Engine::JsonAdapter::SaveCanonical(scenePath, root)) {
			return false;
		}

		std::error_code ec;
		for (auto it = std::filesystem::directory_iterator(actorRoot, ec);
			!ec && it != std::filesystem::directory_iterator{}; it.increment(ec)) {

			if (!it->is_regular_file(ec) ||
				!Engine::Algorithm::EndsWith(
					Engine::Algorithm::ToLower(it->path().filename().string()), ".actor.json") ||
				actorFileNames.contains(it->path().filename().string())) {
				continue;
			}
			std::filesystem::remove(it->path(), ec);
			ec.clear();
		}
		return true;
	}

	uint64_t LocalFileIDOf(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::SceneObjectComponent>(entity)) {
			return 0;
		}
		return world.GetComponent<Engine::SceneObjectComponent>(entity).localFileID.value;
	}

	void SortEntitiesByLocalFileID(Engine::ECSWorld& world, std::vector<Engine::Entity>& entities) {

		for (const Engine::Entity& entity : entities) {
			Engine::SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		}
		std::sort(entities.begin(), entities.end(), [&world](const Engine::Entity& lhs,
			const Engine::Entity& rhs) {

			const uint64_t lhsID = LocalFileIDOf(world, lhs);
			const uint64_t rhsID = LocalFileIDOf(world, rhs);
			if (lhsID != rhsID) {
				return lhsID < rhsID;
			}
			if (lhs.index != rhs.index) {
				return lhs.index < rhs.index;
			}
			return lhs.generation < rhs.generation;
			});
	}

	bool ValidateWorldLocalFileIDs(Engine::ECSWorld& world,
		const std::vector<Engine::Entity>& entities) {

		std::unordered_set<Engine::UUID> ids;
		for (const Engine::Entity& entity : entities) {

			if (!world.IsAlive(entity)) {
				continue;
			}
			Engine::SceneAuthoring::EnsureGameObjectDefaults(world, entity);
			const Engine::UUID localFileID =
				world.GetComponent<Engine::SceneObjectComponent>(entity).localFileID;
			if (!ids.insert(localFileID).second) {
				Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
					"[SceneSystem] LocalFileIDが重複しています ID={}", Engine::ToString(localFileID));
				return false;
			}
		}
		return true;
	}

	bool TryInsertLocalFileID(const nlohmann::json& node, const char* key,
		std::unordered_set<Engine::UUID>& ids) {

		if (!node.is_object()) {
			return false;
		}
		const std::optional<Engine::UUID> parsed =
			Engine::TryParseUUID16Hex(node.value(key, std::string{}));
		return parsed && ids.insert(*parsed).second;
	}

	bool ValidateSerializedLocalFileIDs(const nlohmann::json& root) {

		std::unordered_set<Engine::UUID> ids;
		if (const auto entities = root.find("Entities");
			entities != root.end() && entities->is_array()) {

			for (const auto& entity : *entities) {
				if (!TryInsertLocalFileID(entity, "LocalFileID", ids)) {
					return false;
				}
			}
		}
		if (const auto prefabs = root.find("PrefabInstances");
			prefabs != root.end() && prefabs->is_array()) {

			std::unordered_set<Engine::UUID> instanceIDs;
			for (const auto& prefab : *prefabs) {

				if (!TryInsertLocalFileID(prefab, "InstanceID", instanceIDs)) {
					return false;
				}
				if (const auto entityMap = prefab.find("EntityMap");
					entityMap != prefab.end() && entityMap->is_array()) {
					for (const auto& pair : *entityMap) {
						if (!TryInsertLocalFileID(pair, "S", ids)) {
							return false;
						}
					}
				}
				if (const auto addedEntities = prefab.find("AddedEntities");
					addedEntities != prefab.end() && addedEntities->is_array()) {
					for (const auto& added : *addedEntities) {
						if (!TryInsertLocalFileID(added, "SceneLocalFileID", ids)) {
							return false;
						}
					}
				}
			}
		}
		return true;
	}
}

//============================================================================
//	SceneSystem classMethods
//============================================================================
bool Engine::SceneSystem::LoadScene(const std::filesystem::path& scenePath, ECSWorld& world, AssetDatabase* assetDatabase,
	AssetID sourceAsset, UUID sceneInstanceID, SceneHeader* outHeader, std::vector<Entity>* outCreatedEntities) const {

	// ファイルからnlohmann::jsonをロード
	nlohmann::json root = JsonAdapter::Load(scenePath, true);
	if (!ValidateSceneFileRoot(root)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SceneSystem] 未対応のScene Schemaです 期待値={} scene={}",
			kSceneSchemaVersion, Algorithm::PathToUTF8(scenePath));
		return false;
	}
	if (root.contains("ExternalActors") &&
		!LoadExternalActors(scenePath, sourceAsset, root)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SceneSystem] ExternalActorを読み込めません scene={}",
			Algorithm::PathToUTF8(scenePath));
		return false;
	}
	if (outHeader) {
		if (!FromJson(root["Header"], *outHeader, assetDatabase)) {
			return false;
		}
		// シーン表示名はファイル名を正として、外部リネーム後も古いHeader名を残さない
		if (const std::string assetName = MakeSceneAssetName(scenePath);
			!assetName.empty()) {
			outHeader->name = assetName;
		}
		outHeader->guid = sourceAsset;
		EnsureSceneRenderFeatureProfile(*outHeader,
			Algorithm::PathToUTF8(scenePath), assetDatabase);
	}
	return LoadFromJson(root, world, assetDatabase, sourceAsset, sceneInstanceID, outCreatedEntities);
}

bool Engine::SceneSystem::SaveScene(const std::filesystem::path& scenePath, ECSWorld& world,
	const SceneHeader& header, AssetDatabase& database, const std::vector<Entity>* entitiesSubset) const {

	SceneSaveSnapshot snapshot{};
	if (!CaptureSaveSnapshot(scenePath, world, header,
		database, snapshot, entitiesSubset)) {
		return false;
	}
	return WriteSaveSnapshot(std::move(snapshot));
}

bool Engine::SceneSystem::CaptureSaveSnapshot(
	const std::filesystem::path& scenePath, ECSWorld& world,
	const SceneHeader& header, AssetDatabase& database,
	SceneSaveSnapshot& outSnapshot,
	const std::vector<Entity>* entitiesSubset) const {

	std::vector<Entity> sceneEntities;
	if (entitiesSubset) {
		sceneEntities = *entitiesSubset;
	} else {
		world.ForEachAliveEntity([&sceneEntities](Entity entity) {
			sceneEntities.emplace_back(entity);
			});
	}
	if (!ValidateWorldLocalFileIDs(world, sceneEntities)) {
		return false;
	}

	nlohmann::json root = nlohmann::json::object();
	root["SchemaVersion"] = kSceneSchemaVersion;
	root["Header"] = ToJson(header);

	// 対象実体の中からプレファブインスタンスを集め、薄い差分形式で保存する
	// インスタンスに取り込まれた実体のシーンローカルIDを覚えておき、fat側の重複保存を防ぐ
	std::unordered_set<UUID> consumedSceneLocalIDs;
	std::unordered_map<UUID, AssetID> instanceToPrefab;

	auto collectInstanceIDs = [&](const Entity& entity) {

		if (!world.HasComponent<PrefabLinkComponent>(entity)) {
			return;
		}
		const auto& link = world.GetComponent<PrefabLinkComponent>(entity);
		instanceToPrefab[link.prefabInstanceID] = link.prefabAsset;
		};
	if (entitiesSubset) {
		for (const Entity& entity : *entitiesSubset) {
			if (world.IsAlive(entity)) {
				collectInstanceIDs(entity);
			}
		}
	} else {
		world.ForEachAliveEntity(collectInstanceIDs);
	}

	// プレファブインスタンスごとに差分を抽出する
	nlohmann::json prefabInstances = nlohmann::json::array();
	std::vector<std::pair<UUID, AssetID>> sortedInstances(
		instanceToPrefab.begin(), instanceToPrefab.end());
	std::sort(sortedInstances.begin(), sortedInstances.end(), [](const auto& lhs, const auto& rhs) {
		return lhs.first.value < rhs.first.value;
		});
	for (const auto& [instanceID, prefabAsset] : sortedInstances) {

		const std::vector<Entity> instanceEntities =
			PrefabOverrideUtility::CollectInstanceEntities(world, instanceID);
		const bool hasRoot = std::any_of(instanceEntities.begin(), instanceEntities.end(), [&](const Entity& entity) {
			return world.IsAlive(entity) && world.HasComponent<PrefabLinkComponent>(entity) &&
				world.GetComponent<PrefabLinkComponent>(entity).isPrefabRoot;
			});
		if (!hasRoot) {

			// ルートを失ったPrefabの残存実体をfat側へ保存しない
			for (const Entity& member : instanceEntities) {
				for (const Entity& entity : HierarchyUtility::CollectLogicalSubtree(world, member)) {
					if (world.HasComponent<SceneObjectComponent>(entity)) {
						consumedSceneLocalIDs.insert(
							world.GetComponent<SceneObjectComponent>(entity).localFileID);
					}
				}
			}
			continue;
		}

		const auto base = PrefabOverrideUtility::LoadPrefabBaseEntities(database, prefabAsset);
		PrefabInstanceData data = PrefabOverrideUtility::CaptureInstance(world, instanceID, base);
		data.prefabAsset = prefabAsset;

		// このインスタンスが取り込んだ実体のシーンローカルIDを記録する
		for (const auto& [prefabLocal, sceneLocal] : data.entityMap) {
			consumedSceneLocalIDs.insert(sceneLocal);
		}
		for (const auto& added : data.addedEntities) {
			consumedSceneLocalIDs.insert(added.sceneLocalFileID);
		}
		prefabInstances.push_back(ToJson(data));
	}
	root["PrefabInstances"] = std::move(prefabInstances);

	// インスタンスに取り込まれなかった実体だけをfat保存する
	std::vector<Entity> fatEntities;
	auto appendFatEntity = [&](const Entity& entity) {

		if (!world.IsAlive(entity)) {
			return;
		}
		if (world.HasComponent<SceneObjectComponent>(entity) &&
			consumedSceneLocalIDs.count(world.GetComponent<SceneObjectComponent>(entity).localFileID)) {
			return;
		}
		fatEntities.emplace_back(entity);
		};
	if (entitiesSubset) {
		for (const Entity& entity : *entitiesSubset) {
			appendFatEntity(entity);
		}
	} else {
		world.ForEachAliveEntity(appendFatEntity);
	}
	root["Entities"] = SerializeEntities(world, &fatEntities);

	outSnapshot.scenePath = scenePath;
	outSnapshot.sceneAsset = header.guid;
	outSnapshot.root = std::move(root);
	outSnapshot.useExternalActors =
		ShouldUseExternalActors(scenePath);
	return true;
}

bool Engine::SceneSystem::WriteSaveSnapshot(
	SceneSaveSnapshot snapshot) {

	if (snapshot.scenePath.empty() ||
		!snapshot.root.is_object()) {
		return false;
	}
	if (snapshot.sceneAsset &&
		snapshot.useExternalActors) {
		return SaveExternalActors(snapshot.scenePath,
			snapshot.sceneAsset, snapshot.root);
	}
	snapshot.root.erase("ExternalActors");
	if (!JsonAdapter::SaveCanonical(
		snapshot.scenePath, snapshot.root)) {
		return false;
	}
	if (snapshot.sceneAsset) {
		RemoveExternalActors(
			snapshot.scenePath, snapshot.sceneAsset);
	}
	return true;
}

nlohmann::json Engine::SceneSystem::SerializeEntities(ECSWorld& world, const std::vector<Entity>* subset) const {

	nlohmann::json array = nlohmann::json::array();
	std::vector<Entity> entities;

	if (subset) {
		for (const Entity& entity : *subset) {
			if (world.IsAlive(entity)) {
				entities.emplace_back(entity);
			}
		}
	} else {
		world.ForEachAliveEntity([&](const Entity& entity) {
			entities.emplace_back(entity);
			});
	}
	SortEntitiesByLocalFileID(world, entities);

	for (const Entity& entity : entities) {
		auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
		nlohmann::json entityJson = nlohmann::json::object();
		entityJson["LocalFileID"] = ToString(sceneObject.localFileID);

		nlohmann::json components = nlohmann::json::object();
		world.SerializeEntityComponents(entity, components);
		entityJson["Components"] = std::move(components);
		array.push_back(std::move(entityJson));
	}
	return array;
}

bool Engine::SceneSystem::LoadFromJson(const nlohmann::json& root, ECSWorld& world,
	AssetDatabase* assetDatabase, AssetID sourceAsset, UUID sceneInstanceID,
	std::vector<Entity>* outCreatedEntities) const {

	// ルートがオブジェクトでなければ失敗
	if (!root.is_object()) {
		return false;
	}
	if (!ValidateSerializedLocalFileIDs(root)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SceneSystem] シーン内に不正または重複したLocalFileIDがあります");
		return false;
	}

	// fat保存された実体を読み込む、Entitiesが配列でなければ空として扱いPrefabInstancesのみ処理する
	const nlohmann::json emptyArray = nlohmann::json::array();
	const nlohmann::json& entitiesNode =
		(root.contains("Entities") && root["Entities"].is_array()) ? root["Entities"] : emptyArray;

	// "Entities"配列をループしてエンティティを作成し、コンポーネントを追加する
	for (const auto& entityJson : entitiesNode) {

		const UUID localFileID =
			FromString16Hex(entityJson.value("LocalFileID", std::string{}));

		const nlohmann::json* components =
			(entityJson.contains("Components") && entityJson["Components"].is_object()) ?
			&entityJson["Components"] : nullptr;
		std::vector<uint32_t> componentTypeIDs;
		if (components) {
			componentTypeIDs.reserve(components->size());
			for (auto it = components->begin(); it != components->end(); ++it) {

				const ComponentTypeInfo* info =
					ComponentTypeRegistry::GetInstance().FindByName(it.key());
				if (!info) {
					Logger::Output(LogType::Engine, spdlog::level::err,
						"[SceneSystem] 未登録のComponentTypeです: {}", it.key());
					return false;
				}
				componentTypeIDs.emplace_back(info->id);
			}
		}

		// JSONに含まれるコンポーネントを含む最終アーキタイプへ直接作成
		Entity entity = SceneAuthoring::CreateGameObject(world, "Entity", componentTypeIDs);

		// 作成されたエンティティを出力する
		if (outCreatedEntities) {

			outCreatedEntities->emplace_back(entity);
		}

		// "Components"オブジェクトが存在する場合はコンポーネントを追加する
		if (components) {

			for (auto it = components->begin(); it != components->end(); ++it) {

				const std::string& typeName = it.key();
				const nlohmann::json& data = it.value();
				world.ApplyComponentJson(entity, typeName, data);
			}
		}
		// JSONからSceneObjectを読み直した後に、ランタイム所属情報を設定する
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		{
			auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			sceneObject.localFileID = localFileID;
			sceneObject.sourceAsset = sourceAsset;
			sceneObject.sceneInstanceID = sceneInstanceID;
		}
		// ロード直後に、ワールド実体のサブメッシュを正規化する
		if (assetDatabase && world.HasComponent<MeshRendererComponent>(entity)) {

			MeshSubMeshAuthoring::SyncEntity(assetDatabase, world, entity, true);
		}
	}

	// 薄い差分形式で保存されたプレファブインスタンスを展開する
	if (assetDatabase && root.contains("PrefabInstances") && root["PrefabInstances"].is_array()) {

		HierarchySystem hierarchySystem{};
		for (const auto& instanceJson : root["PrefabInstances"]) {

			PrefabInstanceData data{};
			if (!FromJson(instanceJson, data)) {
				continue;
			}
			const Entity instanceRoot =
				PrefabOverrideUtility::RebuildInstance(world, *assetDatabase, hierarchySystem, data, sceneInstanceID);
			if (!world.IsAlive(instanceRoot)) {
				continue;
			}
			// 生成したインスタンスの実体を作成リストへ加える、追加実体はsceneInstanceIDで保存時に回収される
			if (outCreatedEntities) {
				for (const Entity& entity : PrefabOverrideUtility::CollectInstanceEntities(world, data.instanceID)) {
					outCreatedEntities->emplace_back(entity);
				}
			}
		}
	}

	// 全Prefab生成後に外部親参照を含む階層をまとめて解決する
	std::vector<Entity> hierarchyScope;
	hierarchyScope.reserve(world.GetRecordCount());
	world.ForEachAliveEntity([&](Entity entity) {
		hierarchyScope.emplace_back(entity);
		});
	HierarchySystem hierarchySystem{};
	hierarchySystem.RebuildRuntimeLinks(world, hierarchyScope);
	return true;
}
