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
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

//============================================================================
//	SceneSystem classMethods
//============================================================================
bool Engine::SceneSystem::LoadScene(const std::filesystem::path& scenePath, ECSWorld& world, AssetDatabase* assetDatabase,
	AssetID sourceAsset, UUID sceneInstanceID, SceneHeader* outHeader, std::vector<Entity>* outCreatedEntities) const {

	// ファイルからnlohmann::jsonをロード
	nlohmann::json root = JsonAdapter::Load(scenePath, true);
	if (outHeader) {
		if (root.contains("Header") && root["Header"].is_object()) {

			FromJson(root["Header"], *outHeader, assetDatabase);
		} else {

			outHeader->guid = UUID::New();
			outHeader->name = "UntitledScene";
		}
		EnsureScenePostProcessStack(*outHeader, Algorithm::PathToUTF8(scenePath), assetDatabase);
	}
	return LoadFromJson(root, world, assetDatabase, sourceAsset, sceneInstanceID, outCreatedEntities);
}

bool Engine::SceneSystem::SaveScene(const std::filesystem::path& scenePath, ECSWorld& world,
	const SceneHeader& header, const std::vector<Entity>* entitiesSubset, AssetDatabase* database) const {

	nlohmann::json root = nlohmann::json::object();
	root["Header"] = ToJson(header);

	// databaseが無ければ従来通り全実体をfat保存する、後方互換のため
	if (!database) {

		root["Entities"] = SerializeEntities(world, entitiesSubset);
		JsonAdapter::Save(scenePath, root);
		return true;
	}

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
	for (const auto& [instanceID, prefabAsset] : instanceToPrefab) {

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

		const auto base = PrefabOverrideUtility::LoadPrefabBaseEntities(*database, prefabAsset);
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

	JsonAdapter::Save(scenePath, root);
	return true;
}

nlohmann::json Engine::SceneSystem::SerializeEntities(ECSWorld& world, const std::vector<Entity>* subset) const {

	nlohmann::json array = nlohmann::json::array();

	// subsetが指定されている場合はそのエンティティのみをシリアライズし、subset内のエンティティが存在しない場合は無視する
	if (subset) {
		for (const Entity& entity : *subset) {

			// エンティティが存在しない場合は無視
			if (!world.IsAlive(entity)) {
				continue;
			}

			// デフォルトコンポーネントの追加
			SceneAuthoring::EnsureGameObjectDefaults(world, entity);
			auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);

			// エンティティのシリアライズ
			nlohmann::json entityJson = nlohmann::json::object();
			entityJson["LocalFileID"] = ToString(sceneObject.localFileID);

			nlohmann::json components = nlohmann::json::object();
			world.SerializeEntityComponents(entity, components);
			entityJson["Components"] = components;

			// エンティティのnlohmann::jsonを配列に追加
			array.push_back(entityJson);
		}
		return array;
	}

	// subsetが指定されていない場合は全てのエンティティをシリアライズする
	world.ForEachAliveEntity([&](const Entity& entity) {

		// デフォルトコンポーネントの追加
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);

		// エンティティのシリアライズ
		nlohmann::json entityJson = nlohmann::json::object();
		entityJson["LocalFileID"] = ToString(sceneObject.localFileID);

		nlohmann::json components = nlohmann::json::object();
		world.SerializeEntityComponents(entity, components);
		entityJson["Components"] = components;

		// エンティティのnlohmann::jsonを配列に追加
		array.push_back(entityJson);
		});
	return array;
}

bool Engine::SceneSystem::LoadFromJson(const nlohmann::json& root, ECSWorld& world,
	AssetDatabase* assetDatabase, AssetID sourceAsset, UUID sceneInstanceID,
	std::vector<Entity>* outCreatedEntities) const {

	// ルートがオブジェクトでなければ失敗
	if (!root.is_object()) {
		return false;
	}

	// fat保存された実体を読み込む、Entitiesが配列でなければ空として扱いPrefabInstancesのみ処理する
	const nlohmann::json emptyArray = nlohmann::json::array();
	const nlohmann::json& entitiesNode =
		(root.contains("Entities") && root["Entities"].is_array()) ? root["Entities"] : emptyArray;

	// "Entities"配列をループしてエンティティを作成し、コンポーネントを追加する
	for (const auto& entityJson : entitiesNode) {

		// "LocalFileID"が存在しない場合は"UUID"を探し、どちらも存在しない場合はSceneObject側の値を使う
		std::string localFileIDStr = entityJson.value("LocalFileID", entityJson.value("UUID", ""));
		UUID localFileID = localFileIDStr.empty() ? UUID{} : FromString16Hex(localFileIDStr);

		// エンティティの作成
		Entity entity = world.CreateEntity();
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);

		// 作成されたエンティティを出力する
		if (outCreatedEntities) {

			outCreatedEntities->emplace_back(entity);
		}

		// "Components"オブジェクトが存在する場合はコンポーネントを追加する
		if (entityJson.contains("Components") && entityJson["Components"].is_object()) {

			const auto& components = entityJson["Components"];
			for (auto it = components.begin(); it != components.end(); ++it) {

				const std::string& typeName = it.key();
				const nlohmann::json& data = it.value();
				world.AddComponentFromJson(entity, typeName, data);
			}
		}
		// JSONからSceneObjectを読み直した後に、ランタイム所属情報を設定する
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		{
			auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			if (localFileID) {

				sceneObject.localFileID = localFileID;
			} else if (!sceneObject.localFileID) {

				sceneObject.localFileID = UUID::New();
			}
			sceneObject.sourceAsset = sourceAsset;
			sceneObject.sceneInstanceID = sceneInstanceID;
		}
		// ロード直後に、ワールド実体のサブメッシュを正規化する
		if (assetDatabase && world.HasComponent<MeshRendererComponent>(entity)) {

			auto& meshRenderer = world.GetComponent<MeshRendererComponent>(entity);
			MeshSubMeshAuthoring::SyncComponent(assetDatabase, meshRenderer, true);
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
