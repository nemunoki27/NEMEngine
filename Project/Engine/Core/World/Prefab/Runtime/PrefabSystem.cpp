#include "PrefabSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

//============================================================================
//	PrefabSystem classMethods
//============================================================================
namespace {

	constexpr uint32_t kPrefabSchemaVersion = 2;
	constexpr uint32_t kMinimumPrefabSchemaVersion = 1;
	constexpr uint32_t kMaximumNestedPrefabDepth = 32;

	// JSON値からローカルIDを読み取る
	static Engine::UUID ReadLocalFileID(const nlohmann::json& value) {

		if (!value.is_string()) {
			return Engine::UUID{};
		}
		const std::string raw = value.get<std::string>();
		return raw.empty() ? Engine::UUID{} : Engine::FromString16Hex(raw);
	}

	// プレファブファイル内でエンティティを識別するためのIDを読み取る
	static Engine::UUID ReadEntityLocalFileID(const nlohmann::json& entityJson) {

		if (!entityJson.is_object() || !entityJson.contains("LocalFileID")) {
			return Engine::UUID{};
		}
		return ReadLocalFileID(entityJson["LocalFileID"]);
	}

	// ジョイント参照が同じプレファブ内に存在するか判定する
	static bool HasPrefabLocalJointTarget(const nlohmann::json& component,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& prefabLocalToSceneLocal) {

		if (!component.is_object() || !component.contains("skinnedEntityLocalFileID")) {
			return false;
		}
		const Engine::UUID targetLocalFileID = ReadLocalFileID(component["skinnedEntityLocalFileID"]);
		return targetLocalFileID && prefabLocalToSceneLocal.contains(targetLocalFileID);
	}

	// 保存時に使うPrefab内ローカルIDを解決する
	static Engine::UUID ResolvePrefabLocalFileID(Engine::ECSWorld& world, const Engine::Entity& entity,
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

	// SceneローカルIDからPrefabローカルIDへの変換表を作る
	static Engine::PrefabReferenceRemapper::LocalFileIDMap BuildSceneToPrefabLocalMap(
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

	// 保存ルート直下にあるネストPrefabのルートだけを集める
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
}

void Engine::PrefabSystem::SetPrefabLink(ECSWorld& world, const Entity& entity, AssetID prefabAsset,
	UUID prefabLocalFileID, UUID prefabInstanceID, bool isPrefabRoot,
	UUID ownerPrefabInstanceID, UUID nestedSlotID, bool isPrefabAssetNested) const {

	if (!world.IsAlive(entity)) {
		return;
	}
	auto& prefabLink = world.HasComponent<PrefabLinkComponent>(entity) ?
		world.GetComponent<PrefabLinkComponent>(entity) :
		world.AddComponent<PrefabLinkComponent>(entity);
	prefabLink.prefabAsset = prefabAsset;
	prefabLink.prefabLocalFileID = prefabLocalFileID;
	prefabLink.prefabInstanceID = prefabInstanceID;
	prefabLink.ownerPrefabInstanceID = ownerPrefabInstanceID;
	prefabLink.nestedSlotID = nestedSlotID;
	prefabLink.isPrefabAssetNested = isPrefabAssetNested;
	prefabLink.isPrefabRoot = isPrefabRoot;
}

Engine::UUID Engine::PrefabSystem::SetPrefabLinkToSubtree(ECSWorld& world, const Entity& root,
	AssetID prefabAsset, UUID prefabInstanceID) const {

	if (!world.IsAlive(root) || !prefabAsset) {
		return UUID{};
	}
	const UUID resolvedInstanceID = prefabInstanceID ? prefabInstanceID : UUID::New();
	auto linkSubtree = [&](auto&& self, const Entity& entity, bool isRoot) -> void {

		if (!world.IsAlive(entity)) {
			return;
		}
		if (!isRoot && world.HasComponent<PrefabLinkComponent>(entity)) {
			return;
		}
		if (world.HasComponent<SceneObjectComponent>(entity)) {

			const UUID prefabLocalFileID = world.GetComponent<SceneObjectComponent>(entity).localFileID;
			SetPrefabLink(world, entity, prefabAsset, prefabLocalFileID, resolvedInstanceID, isRoot);
		}
		if (!world.HasComponent<HierarchyComponent>(entity)) {
			return;
		}
		Entity child = world.GetComponent<HierarchyComponent>(entity).firstChild;
		while (world.IsAlive(child)) {

			const Entity next = world.HasComponent<HierarchyComponent>(child) ?
				world.GetComponent<HierarchyComponent>(child).nextSibling : Entity::Null();
			self(self, child, false);
			child = next;
		}
		};
	linkSubtree(linkSubtree, root, true);
	return resolvedInstanceID;
}

bool Engine::PrefabSystem::UnpackPrefabInstance(
	ECSWorld& world, const Entity& root, PrefabUnpackMode mode) const {

	if (!world.IsAlive(root) || !world.HasComponent<PrefabLinkComponent>(root)) {
		return false;
	}
	const PrefabLinkComponent rootLink = world.GetComponent<PrefabLinkComponent>(root);
	if (!rootLink.isPrefabRoot || !rootLink.prefabInstanceID) {
		return false;
	}

	if (mode == PrefabUnpackMode::Completely) {

		const std::vector<Entity> entities = HierarchyUtility::CollectLogicalSubtree(world, root);
		for (const Entity& entity : entities) {

			if (world.IsAlive(entity) && world.HasComponent<PrefabLinkComponent>(entity)) {
				world.RemoveComponent<PrefabLinkComponent>(entity);
			}
		}
		return true;
	}

	std::vector<Entity> instanceEntities;
	std::vector<std::pair<Entity, PrefabLinkComponent>> nestedUpdates;
	std::unordered_map<UUID, UUID> nestedSlots;
	world.ForEach<PrefabLinkComponent>([&](const Entity& entity, PrefabLinkComponent& link) {

		if (link.prefabInstanceID == rootLink.prefabInstanceID) {
			instanceEntities.emplace_back(entity);
			return;
		}
		if (link.ownerPrefabInstanceID != rootLink.prefabInstanceID) {
			return;
		}

		PrefabLinkComponent updated = link;
		updated.ownerPrefabInstanceID = rootLink.ownerPrefabInstanceID;
		if (rootLink.ownerPrefabInstanceID) {

			auto [it, inserted] = nestedSlots.try_emplace(link.prefabInstanceID, UUID{});
			if (inserted) {
				it->second = UUID::New();
			}
			updated.nestedSlotID = it->second;
			updated.isPrefabAssetNested = false;
		} else {

			updated.nestedSlotID = UUID{};
			updated.isPrefabAssetNested = false;
		}
		nestedUpdates.emplace_back(entity, updated);
		});

	for (const auto& [entity, link] : nestedUpdates) {

		if (world.IsAlive(entity) && world.HasComponent<PrefabLinkComponent>(entity)) {
			world.GetComponent<PrefabLinkComponent>(entity) = link;
			world.MarkComponentModified<PrefabLinkComponent>(entity);
		}
	}
	for (const Entity& entity : instanceEntities) {

		if (world.IsAlive(entity) && world.HasComponent<PrefabLinkComponent>(entity)) {
			world.RemoveComponent<PrefabLinkComponent>(entity);
		}
	}
	return !instanceEntities.empty();
}

bool Engine::PrefabSystem::SavePrefab(AssetDatabase& database, ECSWorld& world,
	const Entity& root, const std::string& prefabAssetPath, UUID prefabInstanceID) const {

	// ルートエンティティが存在するか
	if (!world.IsAlive(root)) {
		return false;
	}
	// rootのサブツリーを集めて保存する
	const std::vector<Entity> subtree = HierarchyUtility::CollectLogicalSubtree(world, root);
	return SavePrefabFromEntities(database, world, root, subtree, prefabAssetPath, prefabInstanceID);
}

bool Engine::PrefabSystem::SavePrefabFromEntities(AssetDatabase& database, ECSWorld& world, const Entity& root,
	const std::vector<Entity>& entities, const std::string& prefabAssetPath, UUID prefabInstanceID) const {

	if (!world.IsAlive(root) || entities.empty()) {
		return false;
	}

	// プレファブアセットを登録
	const AssetID prefabAsset = database.ImportOrGet(prefabAssetPath, AssetType::Prefab);

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
	const PrefabReferenceRemapper::LocalFileIDMap sceneToPrefabLocal =
		BuildSceneToPrefabLocalMap(world, directEntities, prefabAsset);

	const UUID rootLocalFileID = ResolvePrefabLocalFileID(world, root, prefabAsset);

	// プレファブファイルの構築
	PrefabHeader header{};
	header.guid = prefabAsset;
	header.name = BuildDefaultPrefabName(world, root, prefabAssetPath);
	header.rootLocalFileID = rootLocalFileID;
	header.version = kPrefabSchemaVersion;

	nlohmann::json fileJson = nlohmann::json::object();

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
		if (auto parentIt = sceneToPrefabLocal.find(data.rootParentSceneLocalFileID);
			parentIt != sceneToPrefabLocal.end()) {
			data.rootParentSceneLocalFileID = parentIt->second;
		}
		fileJson["NestedPrefabInstances"].push_back(ToJson(data));
	}
	PrefabReferenceRemapper::NormalizePrefabFileHierarchy(fileJson);
	PrefabReferenceRemapper::NormalizePrefabFileJointAttachments(fileJson);

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

bool Engine::PrefabSystem::InstantiatePrefab(AssetDatabase& database, HierarchySystem& hierarchySystem,
	ECSWorld& world, AssetID prefabAsset, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc) const {

	// 空にする
	outResult = PrefabInstantiateResult{};

	// プレファブアセットが存在するか
	auto fullPath = database.ResolveFullPath(prefabAsset);
	if (fullPath.empty()) {
		return false;
	}

	// ファイルからnlohmann::json読み込み
	nlohmann::json fileJson = JsonAdapter::Load(fullPath);
	if (!fileJson.is_object()) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[PrefabSystem] Prefabファイルを読み込めませんでした AssetID={} Path={}",
			ToString(prefabAsset), fullPath.string());
		return false;
	}
	const uint32_t schemaVersion = fileJson.value("SchemaVersion", 0u);
	if (
		schemaVersion < kMinimumPrefabSchemaVersion || schemaVersion > kPrefabSchemaVersion ||
		!fileJson.contains("Header") || !fileJson["Header"].is_object() ||
		!fileJson.contains("Entities") || !fileJson["Entities"].is_array()) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[PrefabSystem] Prefabファイルの形式が不正です AssetID={} Path={}",
			ToString(prefabAsset), fullPath.string());
		return false;
	}
	if (desc.nestedDepth > kMaximumNestedPrefabDepth) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[PrefabSystem] ネストPrefabの深度上限を超えました AssetID={}", ToString(prefabAsset));
		return false;
	}
	PrefabReferenceRemapper::NormalizePrefabFileHierarchy(fileJson);
	PrefabReferenceRemapper::NormalizePrefabFileJointAttachments(fileJson);

	// ファイルからプレファブ読み込み
	PrefabHeader header{};
	if (!FromJson(fileJson["Header"], header)) {
		return false;
	}
	header.guid = prefabAsset;
	if (!header.rootLocalFileID) {

		const bool hasNestedPrefab = fileJson.contains("NestedPrefabInstances") &&
			(!fileJson["NestedPrefabInstances"].is_array() || !fileJson["NestedPrefabInstances"].empty());
		if (!fileJson["Entities"].empty() || hasNestedPrefab) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[PrefabSystem] Prefabのルート情報が不正です AssetID={}", ToString(prefabAsset));
			return false;
		}

		outResult.prefabInstanceID = desc.forcedInstanceID ? desc.forcedInstanceID : UUID::New();
		const std::string rootName = header.name.empty() ? "NewPrefab" : header.name;
		outResult.root = SceneAuthoring::CreateGameObject(world, rootName);
		auto& sceneObject = world.GetComponent<SceneObjectComponent>(outResult.root);
		sceneObject.localFileID = AllocateUniqueLocalFileID(world);
		sceneObject.sourceAsset = prefabAsset;
		sceneObject.sceneInstanceID = desc.ownerSceneInstanceID;
		SetPrefabLink(world, outResult.root, prefabAsset, sceneObject.localFileID,
			outResult.prefabInstanceID, true, desc.ownerPrefabInstanceID,
			desc.nestedSlotID, desc.isPrefabAssetNested);
		outResult.createdEntities.emplace_back(outResult.root);
		outResult.sourceLocalToEntity.emplace(sceneObject.localFileID, outResult.root);
		if (world.IsAlive(desc.parent)) {
			hierarchySystem.SetParent(world, outResult.root, desc.parent);
		}
		return true;
	}

	std::unordered_set<UUID> prefabLocalFileIDs;
	for (const auto& entityJson : fileJson["Entities"]) {

		const UUID localFileID = ReadEntityLocalFileID(entityJson);
		if (!localFileID || !entityJson.contains("Components") ||
			!entityJson["Components"].is_object() ||
			!prefabLocalFileIDs.insert(localFileID).second) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[PrefabSystem] Prefab内のEntity情報が不正です AssetID={}", ToString(prefabAsset));
			return false;
		}
		for (auto it = entityJson["Components"].begin(); it != entityJson["Components"].end(); ++it) {

			if (it.key() == "JointAttachment") {
				continue;
			}
			if (!ComponentTypeRegistry::GetInstance().FindByName(it.key())) {

				Logger::Output(LogType::Engine, spdlog::level::err,
					"[PrefabSystem] 未登録のComponentTypeです AssetID={} Component={}",
					ToString(prefabAsset), it.key());
				return false;
			}
		}
	}
	if (fileJson.contains("NestedPrefabInstances")) {

		if (!fileJson["NestedPrefabInstances"].is_array()) {
			return false;
		}
		std::unordered_set<UUID> nestedSlotIDs;
		for (const auto& nestedJson : fileJson["NestedPrefabInstances"]) {

			PrefabInstanceData nested{};
			if (!FromJson(nestedJson, nested) || !nested.nestedSlotID ||
				!nestedSlotIDs.insert(nested.nestedSlotID).second) {

				Logger::Output(LogType::Engine, spdlog::level::err,
					"[PrefabSystem] ネストPrefab情報が不正です AssetID={}", ToString(prefabAsset));
				return false;
			}
		}
	}
	if (!prefabLocalFileIDs.contains(header.rootLocalFileID)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[PrefabSystem] PrefabのルートEntityが存在しません AssetID={}", ToString(prefabAsset));
		return false;
	}

	// シーン内で一意なプレファブインスタンスIDを生成、薄い保存からの復元では指定IDを使い同一性を保つ
	outResult.prefabInstanceID = desc.forcedInstanceID ? desc.forcedInstanceID : UUID::New();

	// プレファブ内ローカルIDから復元すべきシーンローカルIDを引くための一時マップ
	std::unordered_map<UUID, UUID> remapLookup;
	if (desc.localFileIDRemap) {
		for (const auto& pair : *desc.localFileIDRemap) {
			remapLookup.emplace(pair.first, pair.second);
		}
	}
	std::unordered_map<UUID, UUID> stableUUIDLookup;
	if (desc.stableUUIDRemap) {
		for (const auto& pair : *desc.stableUUIDRemap) {
			stableUUIDLookup.emplace(pair.first, pair.second);
		}
	}

	std::unordered_map<UUID, UUID> prefabLocalToSceneLocal;
	std::vector<std::pair<Entity, const nlohmann::json*>> pendingLoads;
	pendingLoads.reserve(fileJson["Entities"].size());

	//============================================================================
	//	エンティティを作成し、ローカルからシーンのIDへのマップを構築する
	//============================================================================
	for (const auto& entityJson : fileJson["Entities"]) {

		// プレファブファイル内のローカルIDを読み取る
		UUID prefabLocalFileID = ReadEntityLocalFileID(entityJson);

		// 最終シグネチャで作成しPrefab生成中の構造移動を抑える
		std::vector<uint32_t> componentTypeIDs;
		const auto& prefabComponents = entityJson["Components"];
		componentTypeIDs.reserve(prefabComponents.size() + 1);
		componentTypeIDs.emplace_back(
			ComponentTypeRegistry::GetInstance().GetID<PrefabLinkComponent>());
		for (auto it = prefabComponents.begin(); it != prefabComponents.end(); ++it) {

			// JointAttachmentは参照先を解決できたエンティティだけ後から追加する
			if (it.key() == "JointAttachment") {
				continue;
			}
			const ComponentTypeInfo* info =
				ComponentTypeRegistry::GetInstance().FindByName(it.key());
			if (!info) {
				return false;
			}
			componentTypeIDs.emplace_back(info->id);
		}
		UUID stableUUID{};
		if (auto stableIt = stableUUIDLookup.find(prefabLocalFileID);
			stableIt != stableUUIDLookup.end()) {
			stableUUID = stableIt->second;
		}
		const Entity entity = SceneAuthoring::CreateGameObject(
			world, "Entity", componentTypeIDs, stableUUID);
		// 復元時は保存済みのシーンローカルIDを使い、無ければ新規採番する
		UUID newSceneLocalFileID{};
		if (auto remapIt = remapLookup.find(prefabLocalFileID); remapIt != remapLookup.end() && remapIt->second) {
			newSceneLocalFileID = remapIt->second;
		} else {
			newSceneLocalFileID = AllocateUniqueLocalFileID(world);
		}
		if (entityJson.contains("Components") && entityJson["Components"].is_object()) {
			const auto& components = entityJson["Components"];
			if (components.contains("SceneObject") && components["SceneObject"].is_object()) {
				world.AddComponentFromJson(entity, "SceneObject", components["SceneObject"]);
			}
		}
		// シーンオブジェクト初期化
		{
			auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			sceneObject.localFileID = newSceneLocalFileID;
			sceneObject.sourceAsset = prefabAsset;
			sceneObject.sceneInstanceID = desc.ownerSceneInstanceID;
		}
		// プレファブリンク初期化
		SetPrefabLink(world, entity, prefabAsset, prefabLocalFileID,
			outResult.prefabInstanceID, prefabLocalFileID == header.rootLocalFileID,
			desc.ownerPrefabInstanceID, desc.nestedSlotID, desc.isPrefabAssetNested);

		// 作成したエンティティを結果に追加
		outResult.createdEntities.emplace_back(entity);
		outResult.sourceLocalToEntity.emplace(prefabLocalFileID, entity);
		prefabLocalToSceneLocal.emplace(prefabLocalFileID, newSceneLocalFileID);
		pendingLoads.emplace_back(entity, &entityJson);
	}

	//============================================================================
	//	コンポーネントを追加する
	//============================================================================
	for (auto& [entity, entityJson] : pendingLoads) {

		if (!entityJson->contains("Components") || !(*entityJson)["Components"].is_object()) {
			continue;
		}

		const auto& components = (*entityJson)["Components"];
		for (auto it = components.begin(); it != components.end(); ++it) {

			const std::string& typeName = it.key();
			if (typeName == "SceneObject" || typeName == "PrefabLink") {
				continue;
			}
			nlohmann::json data = it.value();
			if (typeName == "JointAttachment") {

				const UUID entityLocalFileID = ReadEntityLocalFileID(*entityJson);
				if (entityLocalFileID == header.rootLocalFileID ||
					!HasPrefabLocalJointTarget(data, prefabLocalToSceneLocal)) {
					continue;
				}
			}
			PrefabReferenceRemapper::RemapComponent(
				typeName, data, prefabLocalToSceneLocal, PrefabReferenceRemapper::ReferenceSpace::Scene, prefabAsset);
			world.AddComponentFromJson(entity, typeName, data);
		}
	}

	//============================================================================
	//	MeshRendererのサブメッシュをmesh実体へ正規化する
	//============================================================================
	for (const Entity& entity : outResult.createdEntities) {

		if (!world.IsAlive(entity) || !world.HasComponent<MeshRendererComponent>(entity)) {
			continue;
		}
		MeshSubMeshAuthoring::SyncEntity(&database, world, entity, true);
	}

	//============================================================================
	//	ヒエラルキーの親参照をローカルIDからシーンのIDへ変換する
	//============================================================================
	for (const Entity& entity : outResult.createdEntities) {

		if (!world.IsAlive(entity) || !world.HasComponent<HierarchyComponent>(entity)) {
			continue;
		}

		auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
		if (!hierarchy.parentLocalFileID) {
			continue;
		}
		auto it = prefabLocalToSceneLocal.find(hierarchy.parentLocalFileID);
		if (it != prefabLocalToSceneLocal.end()) {
			hierarchy.parentLocalFileID = it->second;
		}
	}

	// ランタイムのリンクを再構築する
	hierarchySystem.RebuildRuntimeLinks(world, outResult.createdEntities);

	// ルートエンティティを特定する
	auto rootIt = outResult.sourceLocalToEntity.find(header.rootLocalFileID);
	if (rootIt != outResult.sourceLocalToEntity.end()) {

		outResult.root = rootIt->second;
	}
	// ルートが特定できなかった場合は、作成されたエンティティの先頭をルートとする
	else if (!outResult.createdEntities.empty()) {

		outResult.root = outResult.createdEntities.front();
	}

	// 新規生成のときだけルート名を.prefabのベース名にする、シーン復元では保存済みの名前を尊重する
	if (desc.renameRootToPrefabName && world.IsAlive(outResult.root) &&
		world.HasComponent<NameComponent>(outResult.root)) {

		// プレファブは ".prefab.json" の二重拡張子なので、stemを二段かけて純粋な名前を取り出す
		std::filesystem::path namePath = fullPath.stem();
		if (namePath.extension() == ".prefab") {
			namePath = namePath.stem();
		}
		const std::string baseName = Algorithm::PathToUTF8(namePath);
		if (!baseName.empty()) {

			// 同名インスタンスがあれば name_N へずらす、生成中のルート自身は判定から外す
			world.GetComponent<NameComponent>(outResult.root).name =
				SceneAuthoring::MakeUniqueEntityName(world, baseName, outResult.root);
		}
	}

	// Prefabの実体を単一ルートの階層へ揃える
	if (outResult.root.IsValid()) {
		for (const Entity& entity : outResult.createdEntities) {

			if (entity == outResult.root || !world.IsAlive(entity)) {
				continue;
			}
			// ジョイント接続は論理的にルート配下なので通常の親子関係を重ねない
			if (world.HasComponent<JointAttachmentComponent>(entity)) {
				continue;
			}
			const bool isRoot = !world.HasComponent<HierarchyComponent>(entity) ||
				!world.IsAlive(world.GetComponent<HierarchyComponent>(entity).parent);
			if (isRoot) {
				hierarchySystem.SetParent(world, entity, outResult.root);
			}
		}
	}

	// 親が指定されている場合は、ルートを親にぶら下げる
	if (world.IsAlive(desc.parent) && outResult.root.IsValid()) {

		hierarchySystem.SetParent(world, outResult.root, desc.parent);
	}

	// 親Prefabアセットに保存されたネストPrefabを差分付きで生成する
	std::vector<PrefabInstanceData> nestedDeclarations;
	if (fileJson.contains("NestedPrefabInstances") && fileJson["NestedPrefabInstances"].is_array()) {
		for (const auto& nestedJson : fileJson["NestedPrefabInstances"]) {

			PrefabInstanceData nested{};
			if (FromJson(nestedJson, nested)) {
				nestedDeclarations.emplace_back(std::move(nested));
			}
		}
	}

	std::unordered_set<UUID> restoredSlots;
	auto findRestoredNested = [&](UUID nestedSlotID) -> const PrefabInstanceData* {

		if (!desc.nestedInstanceRemap) {
			return nullptr;
		}
		for (const auto& nested : *desc.nestedInstanceRemap) {
			if (nested.nestedSlotID == nestedSlotID) {
				return &nested;
			}
		}
		return nullptr;
		};

	auto freshenNestedData = [&](auto&& self, PrefabInstanceData& data, UUID ownerInstanceID,
		const PrefabReferenceRemapper::LocalFileIDMap& externalMap, bool preserveLocalFileIDs) -> void {

		PrefabReferenceRemapper::LocalFileIDMap localMap;
		for (auto& [prefabLocalFileID, sceneLocalFileID] : data.entityMap) {

			const UUID previous = sceneLocalFileID;
			if (!preserveLocalFileIDs) {
				sceneLocalFileID = AllocateUniqueLocalFileID(world);
			}
			localMap.emplace(previous, sceneLocalFileID);
		}
		for (auto& added : data.addedEntities) {

			const UUID previous = added.sceneLocalFileID;
			if (!preserveLocalFileIDs) {
				added.sceneLocalFileID = AllocateUniqueLocalFileID(world);
			}
			localMap.emplace(previous, added.sceneLocalFileID);
		}
		for (const auto& [source, target] : externalMap) {
			localMap.try_emplace(source, target);
		}

		auto remapLocal = [&](UUID value) {
			auto it = localMap.find(value);
			return it != localMap.end() ? it->second : value;
			};
		data.rootParentSceneLocalFileID = remapLocal(data.rootParentSceneLocalFileID);
		for (auto& added : data.addedEntities) {

			added.parentSceneLocalFileID = remapLocal(added.parentSceneLocalFileID);
			PrefabReferenceRemapper::RemapComponents(added.components, localMap,
				PrefabReferenceRemapper::ReferenceSpace::Scene, data.prefabAsset);
		}
		for (auto& hierarchyMod : data.hierarchyModifications) {
			hierarchyMod.externalParentSceneLocalFileID =
				remapLocal(hierarchyMod.externalParentSceneLocalFileID);
		}

		data.instanceID = UUID::New();
		data.ownerPrefabInstanceID = ownerInstanceID;
		data.stableUUIDMap.clear();
		for (auto& nested : data.nestedInstances) {
			self(self, nested, data.instanceID, localMap, preserveLocalFileIDs);
		}
		};

	auto instantiateNested = [&](PrefabInstanceData data, bool preserveSceneIDs, bool isPrefabAssetNested) {

		if (!preserveSceneIDs) {
			freshenNestedData(freshenNestedData, data, outResult.prefabInstanceID,
				prefabLocalToSceneLocal, desc.preserveNestedLocalFileIDs);
		} else {
			data.ownerPrefabInstanceID = outResult.prefabInstanceID;
		}
		data.isPrefabAssetNested = isPrefabAssetNested;

		const Entity nestedRoot = PrefabOverrideUtility::RebuildInstance(
			world, database, hierarchySystem, data, desc.ownerSceneInstanceID, desc.nestedDepth + 1);
		if (!world.IsAlive(nestedRoot)) {
			return false;
		}
		for (const Entity& nestedEntity : HierarchyUtility::CollectLogicalSubtree(world, nestedRoot)) {
			if (std::find(outResult.createdEntities.begin(), outResult.createdEntities.end(), nestedEntity) ==
				outResult.createdEntities.end()) {
				outResult.createdEntities.emplace_back(nestedEntity);
			}
		}
		return true;
		};

	bool nestedSucceeded = true;
	for (const auto& declaration : nestedDeclarations) {

		if (desc.removedNestedSlots &&
			std::find(desc.removedNestedSlots->begin(), desc.removedNestedSlots->end(),
				declaration.nestedSlotID) != desc.removedNestedSlots->end()) {
			continue;
		}
		if (const PrefabInstanceData* restored = findRestoredNested(declaration.nestedSlotID)) {
			restoredSlots.insert(declaration.nestedSlotID);
			nestedSucceeded = instantiateNested(*restored, true, true);
		} else {
			nestedSucceeded = instantiateNested(declaration, false, true);
		}
		if (!nestedSucceeded) {
			break;
		}
	}
	if (nestedSucceeded && desc.nestedInstanceRemap) {
		for (const auto& restored : *desc.nestedInstanceRemap) {

			if (restoredSlots.contains(restored.nestedSlotID)) {
				continue;
			}
			if (restored.isPrefabAssetNested) {
				continue;
			}
			if (!instantiateNested(restored, true, false)) {
				nestedSucceeded = false;
				break;
			}
		}
	}
	if (!nestedSucceeded) {

		for (auto it = outResult.createdEntities.rbegin(); it != outResult.createdEntities.rend(); ++it) {
			if (world.IsAlive(*it)) {
				world.DestroyEntity(*it);
			}
		}
		world.FlushPendingDestroyEntities();
		outResult = PrefabInstantiateResult{};
		return false;
	}
	return true;
}

bool Engine::PrefabSystem::InstantiatePrefabFromPath(AssetDatabase& database, HierarchySystem& hierarchySystem,
	ECSWorld& world, const std::string& prefabAssetPath, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc) const {

	const AssetID prefabAsset = database.ImportOrGet(prefabAssetPath, AssetType::Prefab);
	return InstantiatePrefab(database, hierarchySystem, world, prefabAsset, outResult, desc);
}

Engine::UUID Engine::PrefabSystem::AllocateUniqueLocalFileID(ECSWorld& world) const {

	while (true) {

		UUID candidate = UUID::New();
		bool exists = false;
		// このローカルIDがすでにシーン内のどこかで使われていないか走査する
		world.ForEach<SceneObjectComponent>([&](const Entity&, SceneObjectComponent& sceneObject) {
			if (sceneObject.localFileID == candidate) {
				exists = true;
			}
			});
		if (!exists) {
			return candidate;
		}
	}
}

std::string Engine::PrefabSystem::BuildDefaultPrefabName(ECSWorld& world,
	const Entity& root, const std::string& prefabAssetPath) const {

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
