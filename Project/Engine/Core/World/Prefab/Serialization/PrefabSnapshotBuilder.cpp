#include "PrefabSnapshotBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Serialization/PrefabHeader.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabDocument.h>

// c++
#include <unordered_set>

using namespace Engine::PrefabDocument;

namespace {

	Engine::UUID ResolvePrefabLocalFileID(Engine::ECSWorld& world, const Engine::Entity& entity,
		Engine::AssetID prefabAsset) {

		if (world.HasComponent<Engine::PrefabLinkComponent>(entity)) {

			const auto& prefabLink = world.GetComponent<Engine::PrefabLinkComponent>(entity);
			if (prefabLink.prefabAsset == prefabAsset && prefabLink.prefabLocalFileID) {
				return prefabLink.prefabLocalFileID;
			}
		}
		if (!world.HasComponent<Engine::SceneObjectComponent>(entity)) {
			return Engine::UUID{};
		}
		return world.GetComponent<Engine::SceneObjectComponent>(entity).localFileID;
	}

	Engine::PrefabReferenceRemapper::LocalFileIDMap BuildSceneToPrefabLocalMap(
		Engine::ECSWorld& world, const std::vector<Engine::Entity>& entities, Engine::AssetID prefabAsset) {

		Engine::PrefabReferenceRemapper::LocalFileIDMap result;
		for (const Engine::Entity& entity : entities) {

			if (!world.IsAlive(entity) ||
				!world.HasComponent<Engine::SceneObjectComponent>(entity)) {
				continue;
			}
			Engine::SceneAuthoring::EnsureGameObjectDefaults(world, entity);
			const auto& sceneObject = world.GetComponent<Engine::SceneObjectComponent>(entity);
			const Engine::UUID prefabLocalFileID = ResolvePrefabLocalFileID(world, entity, prefabAsset);
			if (sceneObject.localFileID && prefabLocalFileID) {
				result.emplace(sceneObject.localFileID, prefabLocalFileID);
			}
		}
		return result;
	}

	uint64_t EntityKey(const Engine::Entity& entity) {

		return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	}

	std::vector<Engine::Entity> CollectImmediateNestedPrefabRoots(
		Engine::ECSWorld& world, const Engine::Entity& root, const std::vector<Engine::Entity>& entities) {

		std::vector<Engine::Entity> result;
		for (const Engine::Entity& entity : entities) {

			if (entity == root || !world.IsAlive(entity) ||
				!world.HasComponent<Engine::PrefabLinkComponent>(entity) ||
				!world.GetComponent<Engine::PrefabLinkComponent>(entity).isPrefabRoot) {
				continue;
			}

			bool reachesRoot = false;
			Engine::Entity ancestor = world.HasComponent<Engine::HierarchyComponent>(entity) ?
				world.GetComponent<Engine::HierarchyComponent>(entity).parent : Engine::Entity::Null();
			size_t remaining = world.GetRecordCount() + 1;
			while (world.IsAlive(ancestor) && remaining-- > 0) {

				if (ancestor == root) {
					reachesRoot = true;
					break;
				}
				if (world.HasComponent<Engine::PrefabLinkComponent>(ancestor) &&
					world.GetComponent<Engine::PrefabLinkComponent>(ancestor).isPrefabRoot) {
					break;
				}
				ancestor = world.HasComponent<Engine::HierarchyComponent>(ancestor) ?
					world.GetComponent<Engine::HierarchyComponent>(ancestor).parent : Engine::Entity::Null();
			}
			if (reachesRoot) {
				result.emplace_back(entity);
			}
		}
		return result;
	}

	void AppendNestedSceneLocalFileIDs(const Engine::PrefabInstanceData& data,
		Engine::PrefabReferenceRemapper::LocalFileIDMap& sceneToPrefabLocal) {

		for (const auto& mapping : data.entityMap) {
			const Engine::UUID sceneLocalFileID = mapping.second;
			if (sceneLocalFileID) {
				sceneToPrefabLocal.try_emplace(sceneLocalFileID, sceneLocalFileID);
			}
		}
		for (const Engine::PrefabAddedEntity& added : data.addedEntities) {
			if (added.sceneLocalFileID) {
				sceneToPrefabLocal.try_emplace(added.sceneLocalFileID, added.sceneLocalFileID);
			}
		}
		for (const Engine::PrefabInstanceData& nested : data.nestedInstances) {
			AppendNestedSceneLocalFileIDs(nested, sceneToPrefabLocal);
		}
	}

	void RemapNestedInstanceReferences(Engine::PrefabInstanceData& data,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& sceneToPrefabLocal,
		Engine::AssetID prefabAsset) {

		auto remapSceneLocalFileID = [&](Engine::UUID value) {
			if (!value) {
				return Engine::UUID{};
			}
			auto it = sceneToPrefabLocal.find(value);
			return it != sceneToPrefabLocal.end() ? it->second : Engine::UUID{};
			};
		data.rootParentSceneLocalFileID =
			remapSceneLocalFileID(data.rootParentSceneLocalFileID);
		for (Engine::PrefabPropertyModification& modification : data.modifications) {
			Engine::PrefabReferenceRemapper::RemapValue(modification.value, modification.path,
				sceneToPrefabLocal, Engine::PrefabReferenceRemapper::ReferenceSpace::Prefab, prefabAsset);
		}
		for (Engine::PrefabComponentModification& added : data.addedComponents) {
			Engine::PrefabReferenceRemapper::RemapComponent(added.type, added.value,
				sceneToPrefabLocal, Engine::PrefabReferenceRemapper::ReferenceSpace::Prefab, prefabAsset);
		}
		for (Engine::PrefabAddedEntity& added : data.addedEntities) {
			added.parentSceneLocalFileID =
				remapSceneLocalFileID(added.parentSceneLocalFileID);
			Engine::PrefabReferenceRemapper::RemapComponents(added.components,
				sceneToPrefabLocal, Engine::PrefabReferenceRemapper::ReferenceSpace::Prefab, prefabAsset);
		}
		for (Engine::PrefabHierarchyModification& hierarchy : data.hierarchyModifications) {
			hierarchy.externalParentSceneLocalFileID =
				remapSceneLocalFileID(hierarchy.externalParentSceneLocalFileID);
		}
		for (Engine::PrefabInstanceData& nested : data.nestedInstances) {
			RemapNestedInstanceReferences(nested, sceneToPrefabLocal, prefabAsset);
		}
	}
}

bool Engine::PrefabSnapshotBuilder::SavePrefabFromEntities(AssetDatabase& database, ECSWorld& world, const Entity& root,
	const std::vector<Entity>& entities, const std::string& prefabAssetPath, UUID prefabInstanceID) {

	if (!world.IsAlive(root) || entities.empty()) {
		return false;
	}

	// プレファブアセットを登録
	const AssetID prefabAsset = database.ImportOrGet(prefabAssetPath, AssetType::Prefab);

	nlohmann::json fileJson;
	if (!Capture(database, world, root, entities, prefabAssetPath, prefabInstanceID, prefabAsset, fileJson)) {
		return false;
	}

	// ファイルに保存
	std::filesystem::path savePath = database.ResolveAssetPath(prefabAssetPath);
	if (savePath.empty()) {
		savePath = prefabAssetPath;
	}
	if (!JsonAdapter::SaveCanonical(savePath, fileJson)) {
		return false;
	}
	PrefabOverrideUtility::InvalidatePrefabBaseCache(prefabAsset);
	return true;
}

std::string Engine::PrefabSnapshotBuilder::BuildDefaultPrefabName(ECSWorld& world,
	const Entity& root, const std::string& prefabAssetPath) {

	// ルートエンティティの名前をベースにする
	if (world.IsAlive(root) && world.HasComponent<NameComponent>(root)) {
		const std::string& name = world.GetComponent<NameComponent>(root).name;
		if (!name.empty()) {
			return name;
		}
	}
	// ルートエンティティの名前が空の場合は、ファイル名をベースにする
	std::filesystem::path path = Algorithm::PathFromUTF8(prefabAssetPath).stem();
	if (path.extension() == ".prefab") {
		path = path.stem();
	}

	return Algorithm::PathToUTF8(path);
}

bool Engine::PrefabSnapshotBuilder::Capture(AssetDatabase& database, ECSWorld& world, const Entity& root,
	const std::vector<Entity>& entities, const std::string& prefabAssetPath, UUID prefabInstanceID,
	AssetID prefabAsset, nlohmann::json& fileJson) {

	// ルート情報をデフォルト構築
	SceneAuthoring::EnsureGameObjectDefaults(world, root);
	PrefabOverrideUtility::SynchronizeNestedPrefabOwnership(world);

	const UUID ownerInstanceID = world.HasComponent<PrefabLinkComponent>(root) ?
		world.GetComponent<PrefabLinkComponent>(root).prefabInstanceID :
		(prefabInstanceID ? prefabInstanceID : UUID::New());
	const std::vector<Entity> nestedRoots = CollectImmediateNestedPrefabRoots(world, root, entities);
	std::unordered_set<uint64_t> nestedEntityKeys;
	for (const Entity& nestedRoot : nestedRoots) {

		auto& rootLink = world.GetComponent<PrefabLinkComponent>(nestedRoot);
		if (rootLink.ownerPrefabInstanceID != ownerInstanceID || !rootLink.nestedSlotID) {
			rootLink.nestedSlotID = UUID::New();
		}
		const UUID nestedSlotID = rootLink.nestedSlotID;
		for (const Entity& member : PrefabOverrideUtility::CollectInstanceEntities(
			world, rootLink.prefabInstanceID)) {

			auto& memberLink = world.GetComponent<PrefabLinkComponent>(member);
			memberLink.ownerPrefabInstanceID = ownerInstanceID;
			memberLink.nestedSlotID = nestedSlotID;
			memberLink.isPrefabAssetNested = true;
		}
		for (const Entity& nestedEntity : HierarchyUtility::CollectLogicalSubtree(world, nestedRoot)) {
			nestedEntityKeys.insert(EntityKey(nestedEntity));
		}
	}

	std::vector<Entity> directEntities;
	directEntities.reserve(entities.size());
	for (const Entity& entity : entities) {
		if (world.IsAlive(entity) && !nestedEntityKeys.contains(EntityKey(entity))) {
			directEntities.emplace_back(entity);
		}
	}
	PrefabReferenceRemapper::LocalFileIDMap sceneToPrefabLocal =
		BuildSceneToPrefabLocalMap(world, directEntities, prefabAsset);
	std::unordered_set<UUID> directLocalIDs;
	for (const Entity& entity : directEntities) {
		const UUID localID = ResolvePrefabLocalFileID(world, entity, prefabAsset);
		if (!localID || !directLocalIDs.insert(localID).second) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[PrefabSystem] IDが重複または不正なため保存を中止します AssetID={} PrefabID={} Entity={}",
				ToString(prefabAsset), ToString(localID), ToString(world.GetUUID(entity)));
			return false;
		}
	}
	std::vector<PrefabInstanceData> nestedInstances;
	nestedInstances.reserve(nestedRoots.size());
	for (const Entity& nestedRoot : nestedRoots) {

		const auto& link = world.GetComponent<PrefabLinkComponent>(nestedRoot);
		const auto base = PrefabOverrideUtility::LoadPrefabBaseEntities(database, link.prefabAsset);
		if (base.empty()) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[PrefabSystem] ネストPrefabを読み込めません AssetID={}", ToString(link.prefabAsset));
			return false;
		}
		PrefabInstanceData data = PrefabOverrideUtility::CaptureInstance(
			world, database, link.prefabInstanceID, base);
		data.ownerPrefabInstanceID = UUID{};
		data.isPrefabAssetNested = true;
		PrefabInstanceData validated;
		if (!FromJson(ToJson(data), validated)) return false;
		AppendNestedSceneLocalFileIDs(data, sceneToPrefabLocal);
		nestedInstances.emplace_back(std::move(data));
	}
	for (PrefabInstanceData& data : nestedInstances) {
		RemapNestedInstanceReferences(data, sceneToPrefabLocal, prefabAsset);
	}

	const UUID rootLocalFileID = ResolvePrefabLocalFileID(world, root, prefabAsset);

	// プレファブファイルの構築
	PrefabHeader header{};
	header.guid = prefabAsset;
	header.name = BuildDefaultPrefabName(world, root, prefabAssetPath);
	header.rootLocalFileID = rootLocalFileID;
	header.version = kPrefabSchemaVersion;

	fileJson = nlohmann::json::object();

	fileJson["SchemaVersion"] = kPrefabSchemaVersion;
	fileJson["Header"] = ToJson(header);
	fileJson["Entities"] = nlohmann::json::array();
	fileJson["NestedPrefabInstances"] = nlohmann::json::array();

	// エンティティごとにコンポーネントをシリアライズしてファイルのnlohmann::jsonに追加
	for (const Entity& entity : directEntities) {

		if (!world.IsAlive(entity) ||
			!world.HasComponent<SceneObjectComponent>(entity)) {
			continue;
		}

		// デフォルトのコンポーネントを構築
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);

		const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
		const UUID prefabLocalFileID = ResolvePrefabLocalFileID(world, entity, prefabAsset);

		// エンティティ
		nlohmann::json entityJson = nlohmann::json::object();
		entityJson["LocalFileID"] = ToString(prefabLocalFileID ? prefabLocalFileID : sceneObject.localFileID);

		// コンポーネント
		nlohmann::json components = nlohmann::json::object();
		world.SerializeEntityComponents(entity, components);
		if (components.contains("SceneObject") && components["SceneObject"].is_object()) {
			components["SceneObject"]["localFileId"] = (prefabLocalFileID ? ToString(prefabLocalFileID) : ToString(sceneObject.localFileID));
		}
		// プレファブルートまたは保存対象外を指すジョイント参照は持ち込まない
		if (world.HasComponent<JointAttachmentComponent>(entity)) {

			const auto& attachment = world.GetComponent<JointAttachmentComponent>(entity);
			if (entity == root || !sceneToPrefabLocal.contains(attachment.skinnedEntityLocalFileID)) {
				components.erase("JointAttachment");
			}
		}
		PrefabReferenceRemapper::RemapComponents(components, sceneToPrefabLocal, PrefabReferenceRemapper::ReferenceSpace::Prefab, prefabAsset);

		// プレファブ自体の中に、別プレファブ由来情報は持ち込まない
		components.erase("PrefabLink");

		// コンポーネントをエンティティに追加
		entityJson["Components"] = std::move(components);
		fileJson["Entities"].push_back(std::move(entityJson));
	}

	// ネストPrefabは元アセットとの差分として保存し、親Prefabへ展開しない
	for (PrefabInstanceData& data : nestedInstances) {

		if (auto parentIt = sceneToPrefabLocal.find(data.rootParentSceneLocalFileID);
			parentIt != sceneToPrefabLocal.end()) {
			data.rootParentSceneLocalFileID = parentIt->second;
		}
		fileJson["NestedPrefabInstances"].push_back(ToJson(data));
	}
	PrefabReferenceRemapper::ClearExternalSceneReferences(fileJson);
	PrefabReferenceRemapper::NormalizePrefabFileHierarchy(fileJson);
	PrefabReferenceRemapper::NormalizePrefabFileJointAttachments(fileJson);

	return true;
}
