#include "PrefabOverrideUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabJsonDiff.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

//============================================================================
//	PrefabOverrideUtility internalMethods
//============================================================================

namespace Engine {
	namespace {

		// 差分の対象外にするコンポーネント、Prefab同一性と階層は別経路で扱う
		const std::vector<std::string> kExcludedDiffTypes = { "PrefabLink", "Hierarchy" };

		// SceneObjectのうちPrefab/Scene内同一性に使う値は比較から外し、編集値だけ差分対象にする
		void NormalizeSceneObjectForDiff(nlohmann::json& components) {

			if (!components.is_object()) {
				return;
			}
			if (!components.contains("SceneObject") || !components["SceneObject"].is_object()) {
				components["SceneObject"] = nlohmann::json::object();
			}
			nlohmann::json& sceneObject = components["SceneObject"];
			sceneObject.erase("localFileId");
			if (!sceneObject.contains("activeSelf")) {
				sceneObject["activeSelf"] = true;
			}
			if (!sceneObject.contains("tag")) {
				sceneObject["tag"] = "Untagged";
			}
			if (!sceneObject.contains("visibilityLayerMask")) {
				sceneObject["visibilityLayerMask"] = 0xFFFFFFFFu;
			}
		}

		void RestoreSceneObjectRuntimeFields(ECSWorld& world, const Entity& entity,
			AssetID sourceAsset, UUID sceneInstanceID, UUID localFileID) {

			if (!world.IsAlive(entity) || !world.HasComponent<SceneObjectComponent>(entity)) {
				return;
			}
			auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			if (localFileID) {
				sceneObject.localFileID = localFileID;
			}
			sceneObject.sourceAsset = sourceAsset;
			sceneObject.sceneInstanceID = sceneInstanceID;
		}

		// エンティティのシーン内ローカルIDを返す
		UUID SceneLocalOf(ECSWorld& world, const Entity& entity) {

			if (!world.IsAlive(entity) || !world.HasComponent<SceneObjectComponent>(entity)) {
				return UUID{};
			}
			return world.GetComponent<SceneObjectComponent>(entity).localFileID;
		}

		// エンティティのランタイム親を返す
		Entity ParentOf(ECSWorld& world, const Entity& entity) {

			if (!world.IsAlive(entity) || !world.HasComponent<HierarchyComponent>(entity)) {
				return Entity::Null();
			}
			return world.GetComponent<HierarchyComponent>(entity).parent;
		}

		// シーンインスタンスとローカルIDからエンティティを線形探索する
		Entity FindBySceneLocal(ECSWorld& world, UUID sceneInstanceID, UUID localFileID) {

			Entity found = Entity::Null();
			if (!localFileID) {
				return found;
			}
			world.ForEachAliveEntity([&](Entity entity) {

				if (found.IsValid() || !world.HasComponent<SceneObjectComponent>(entity)) {
					return;
				}
				const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
				if (sceneObject.sceneInstanceID == sceneInstanceID && sceneObject.localFileID == localFileID) {
					found = entity;
				}
				});
			return found;
		}

		// プレファブ由来でない追加実体を、サブツリーごと収集する
		void CollectAddedSubtree(ECSWorld& world, const Entity& entity, std::vector<PrefabAddedEntity>& out) {

			if (!world.IsAlive(entity)) {
				return;
			}
			if (world.HasComponent<PrefabLinkComponent>(entity)) {

				// 別Prefabや既存Prefab Entityは追加Entityへ展開しない
				return;
			}

			PrefabAddedEntity added{};
			added.sceneLocalFileID = SceneLocalOf(world, entity);
			added.parentSceneLocalFileID = SceneLocalOf(world, ParentOf(world, entity));
			world.SerializeEntityComponents(entity, added.components);
			out.emplace_back(std::move(added));

			// 子も追加実体として再帰収集する
			if (!world.HasComponent<HierarchyComponent>(entity)) {
				return;
			}
			Entity child = world.GetComponent<HierarchyComponent>(entity).firstChild;
			while (world.IsAlive(child)) {

				const Entity next = world.HasComponent<HierarchyComponent>(child) ?
					world.GetComponent<HierarchyComponent>(child).nextSibling : Entity::Null();
				CollectAddedSubtree(world, child, out);
				child = next;
			}
		}

		// インスタンス内のSceneローカルIDからPrefabローカルIDへの変換表を作る
		PrefabReferenceRemapper::LocalFileIDMap BuildSceneToPrefabLocalMap(
			ECSWorld& world, const std::vector<Entity>& instanceEntities) {

			PrefabReferenceRemapper::LocalFileIDMap result;
			for (const Entity& entity : instanceEntities) {

				if (!world.IsAlive(entity) || !world.HasComponent<PrefabLinkComponent>(entity)) {
					continue;
				}
				const UUID sceneLocalFileID = SceneLocalOf(world, entity);
				const UUID prefabLocalFileID = world.GetComponent<PrefabLinkComponent>(entity).prefabLocalFileID;
				if (sceneLocalFileID && prefabLocalFileID) {
					result.emplace(sceneLocalFileID, prefabLocalFileID);
				}
			}
			return result;
		}

		// 生成済みインスタンスからPrefabローカルIDからSceneローカルIDへの変換表を作る
		PrefabReferenceRemapper::LocalFileIDMap BuildPrefabToSceneLocalMap(
			ECSWorld& world, const PrefabInstantiateResult& result) {

			PrefabReferenceRemapper::LocalFileIDMap localMap;
			for (const auto& [prefabLocalFileID, entity] : result.sourceLocalToEntity) {

				if (!world.IsAlive(entity) || !world.HasComponent<SceneObjectComponent>(entity)) {
					continue;
				}
				const UUID sceneLocalFileID = world.GetComponent<SceneObjectComponent>(entity).localFileID;
				if (prefabLocalFileID && sceneLocalFileID) {
					localMap.emplace(prefabLocalFileID, sceneLocalFileID);
				}
			}
			return localMap;
		}
	}
} // namespace Engine

//============================================================================
//	PrefabOverrideUtility classMethods
//============================================================================
std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity> Engine::PrefabOverrideUtility::LoadPrefabBaseEntities(
	AssetDatabase& database, AssetID prefabAsset, UUID* outRootLocalFileID) {

	std::unordered_map<UUID, PrefabBaseEntity> result;

	// プレファブファイルを読み込む
	auto fullPath = database.ResolveFullPath(prefabAsset);
	if (fullPath.empty()) {
		return result;
	}
	nlohmann::json fileJson = JsonAdapter::Load(fullPath.string(), true);
	if (!fileJson.is_object() || !fileJson.contains("Entities") || !fileJson["Entities"].is_array()) {
		return result;
	}
	PrefabReferenceRemapper::RepairPrefabFileScriptRefs(fileJson, prefabAsset);
	PrefabReferenceRemapper::NormalizePrefabFileHierarchy(fileJson);
	PrefabReferenceRemapper::NormalizePrefabFileJointAttachments(fileJson);

	// ルートのローカルIDを取得する
	UUID rootLocalFileID{};
	if (fileJson.contains("Header") && fileJson["Header"].is_object()) {

		const std::string rootStr = fileJson["Header"].value("rootLocalFileID", "");
		rootLocalFileID = rootStr.empty() ? UUID{} : FromString16Hex(rootStr);
	}
	if (outRootLocalFileID) {
		*outRootLocalFileID = rootLocalFileID;
	}

	// 実体ごとにベース情報を構築する
	for (const auto& entityJson : fileJson["Entities"]) {

		const std::string localStr = entityJson.value("LocalFileID", entityJson.value("UUID", ""));
		const UUID localFileID = localStr.empty() ? UUID{} : FromString16Hex(localStr);

		PrefabBaseEntity base{};
		base.localFileID = localFileID;
		base.isRoot = (localFileID == rootLocalFileID);
		if (entityJson.contains("Components") && entityJson["Components"].is_object()) {

			base.components = entityJson["Components"];
			// 親ローカルIDはHierarchyから取り出す
			if (base.components.contains("Hierarchy") && base.components["Hierarchy"].is_object()) {

				const std::string parentStr = base.components["Hierarchy"].value("parentLocalFileID", "");
				base.parentLocalFileID = parentStr.empty() ? UUID{} : FromString16Hex(parentStr);
			}
		}
		result.emplace(localFileID, std::move(base));
	}
	return result;
}

const std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity>&
Engine::PrefabOverrideUtility::LoadPrefabBaseEntitiesCached(AssetDatabase& database, AssetID prefabAsset) {

	// プレファブアセットごとに、最後に読み込んだ時刻と内容を保持する
	struct CacheEntry {

		bool loaded = false;
		std::filesystem::file_time_type writeTime{};
		std::unordered_map<UUID, PrefabBaseEntity> base;
	};
	// エディタは単一スレッドなので関数ローカルstaticで十分
	static std::unordered_map<AssetID, CacheEntry> cache;

	CacheEntry& entry = cache[prefabAsset];

	// ファイルの更新時刻を見て、変化が無ければ読み直さずキャッシュを返す
	const auto fullPath = database.ResolveFullPath(prefabAsset);
	std::error_code ec;
	const std::filesystem::file_time_type currentTime =
		fullPath.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(fullPath, ec);

	if (entry.loaded && !ec && currentTime == entry.writeTime) {
		return entry.base;
	}

	// 初回または更新があった場合だけファイルから読み直す
	entry.base = LoadPrefabBaseEntities(database, prefabAsset);
	entry.writeTime = currentTime;
	entry.loaded = true;
	return entry.base;
}

std::vector<Engine::Entity> Engine::PrefabOverrideUtility::CollectInstanceEntities(ECSWorld& world, UUID instanceID) {

	std::vector<Entity> entities;
	if (!instanceID) {
		return entities;
	}
	world.ForEachAliveEntity([&](Entity entity) {

		if (!world.HasComponent<PrefabLinkComponent>(entity)) {
			return;
		}
		if (world.GetComponent<PrefabLinkComponent>(entity).prefabInstanceID == instanceID) {
			entities.emplace_back(entity);
		}
		});
	return entities;
}

Engine::PrefabInstanceData Engine::PrefabOverrideUtility::CaptureInstance(ECSWorld& world, UUID instanceID,
	const std::unordered_map<UUID, PrefabBaseEntity>& base) {

	PrefabInstanceData data{};
	data.instanceID = instanceID;

	// インスタンスに属するエンティティを集め、プレファブ内ローカルIDから引けるようにする
	const std::vector<Entity> instanceEntities = CollectInstanceEntities(world, instanceID);
	std::unordered_map<UUID, Entity> instanceByPrefabLocal;
	for (const Entity& entity : instanceEntities) {

		const auto& link = world.GetComponent<PrefabLinkComponent>(entity);
		instanceByPrefabLocal.emplace(link.prefabLocalFileID, entity);
		data.prefabAsset = link.prefabAsset;
	}
	const PrefabReferenceRemapper::LocalFileIDMap sceneToPrefabLocal =
		BuildSceneToPrefabLocalMap(world, instanceEntities);

	// 各インスタンスエンティティの差分を抽出する
	for (const Entity& entity : instanceEntities) {

		const auto& link = world.GetComponent<PrefabLinkComponent>(entity);
		const UUID localID = link.prefabLocalFileID;
		data.entityMap.emplace_back(localID, SceneLocalOf(world, entity));

		auto baseIt = base.find(localID);
		// プレファブ側に存在しないインスタンスエンティティはv1では対象外として無視する
		if (baseIt == base.end()) {
			continue;
		}

		// コンポーネント差分
		nlohmann::json instanceComponents;
		world.SerializeEntityComponents(entity, instanceComponents);
		PrefabReferenceRemapper::RemapComponents(
			instanceComponents, sceneToPrefabLocal, PrefabReferenceRemapper::ReferenceSpace::Prefab, data.prefabAsset);
		nlohmann::json baseComponents = baseIt->second.components;
		NormalizeSceneObjectForDiff(baseComponents);
		NormalizeSceneObjectForDiff(instanceComponents);
		const ComponentMapDiff diff = PrefabJsonDiff::DiffComponentMaps(
			baseComponents, instanceComponents, kExcludedDiffTypes);
		for (const auto& [path, value] : diff.modifications) {
			data.modifications.push_back({ localID, path, value });
		}
		for (const auto& [type, value] : diff.addedComponents) {
			data.addedComponents.push_back({ localID, type, value });
		}
		for (const auto& type : diff.removedComponents) {
			data.removedComponents.push_back({ localID, type, nlohmann::json{} });
		}

		// 階層差分
		PrefabHierarchyModification hierarchyMod{};
		hierarchyMod.target = localID;
		bool hasHierarchyOverride = false;

		const Entity parent = ParentOf(world, entity);
		if (link.isPrefabRoot) {

			// ルートが別実体の子になっている場合はインスタンス全体の親として覚える
			if (world.IsAlive(parent)) {
				data.rootParentSceneLocalFileID = SceneLocalOf(world, parent);
			}
		} else {

			// 非ルートはベースの親との差を構造オーバーライドとして扱う
			UUID instanceParentPrefabLocal{};
			bool parentExternal = false;
			UUID externalScene{};
			if (world.IsAlive(parent)) {

				if (world.HasComponent<PrefabLinkComponent>(parent) &&
					world.GetComponent<PrefabLinkComponent>(parent).prefabInstanceID == instanceID) {
					instanceParentPrefabLocal = world.GetComponent<PrefabLinkComponent>(parent).prefabLocalFileID;
				} else {
					parentExternal = true;
					externalScene = SceneLocalOf(world, parent);
				}
			}
			if (parentExternal) {
				hierarchyMod.hasParentOverride = true;
				hierarchyMod.externalParentSceneLocalFileID = externalScene;
				hasHierarchyOverride = true;
			} else if (instanceParentPrefabLocal != baseIt->second.parentLocalFileID) {
				hierarchyMod.hasParentOverride = true;
				hierarchyMod.newParentPrefabLocalFileID = instanceParentPrefabLocal;
				hasHierarchyOverride = true;
			}
		}

		// 兄弟順の差分
		const int32_t instanceSibling = world.HasComponent<HierarchyComponent>(entity) ?
			world.GetComponent<HierarchyComponent>(entity).siblingOrder : 0;
		int32_t baseSibling = 0;
		if (baseIt->second.components.contains("Hierarchy") &&
			baseIt->second.components["Hierarchy"].contains("siblingOrder")) {
			baseSibling = baseIt->second.components["Hierarchy"]["siblingOrder"].get<int32_t>();
		}
		if (instanceSibling != baseSibling) {
			hierarchyMod.hasSiblingOrder = true;
			hierarchyMod.siblingOrder = instanceSibling;
			hasHierarchyOverride = true;
		}

		if (hasHierarchyOverride) {
			data.hierarchyModifications.push_back(hierarchyMod);
		}
	}

	// ベースに在りインスタンスに無いものは削除された実体
	for (const auto& [localID, baseEntity] : base) {

		if (instanceByPrefabLocal.find(localID) == instanceByPrefabLocal.end()) {
			data.removedEntities.push_back(localID);
		}
	}

	// インスタンスエンティティの子のうち、プレファブ由来でないものを追加実体として収集する
	for (const Entity& entity : instanceEntities) {

		if (!world.HasComponent<HierarchyComponent>(entity)) {
			continue;
		}
		Entity child = world.GetComponent<HierarchyComponent>(entity).firstChild;
		while (world.IsAlive(child)) {

			const Entity next = world.HasComponent<HierarchyComponent>(child) ?
				world.GetComponent<HierarchyComponent>(child).nextSibling : Entity::Null();

			const bool isInstanceChild = world.HasComponent<PrefabLinkComponent>(child) &&
				world.GetComponent<PrefabLinkComponent>(child).prefabInstanceID == instanceID;
			if (!isInstanceChild) {
				CollectAddedSubtree(world, child, data.addedEntities);
			}
			child = next;
		}
	}
	return data;
}

Engine::EntityOverrideInfo Engine::PrefabOverrideUtility::CaptureEntityOverride(
	ECSWorld& world, const Entity& entity, AssetDatabase& database) {

	EntityOverrideInfo info{};
	if (!world.IsAlive(entity) || !world.HasComponent<PrefabLinkComponent>(entity)) {
		return info;
	}

	const auto& link = world.GetComponent<PrefabLinkComponent>(entity);
	info.isPrefabInstance = true;
	info.instanceID = link.prefabInstanceID;
	info.prefabLocalFileID = link.prefabLocalFileID;
	info.prefabAsset = link.prefabAsset;

	// ベース実体が見つからなければ差分は出さない
	const auto base = LoadPrefabBaseEntities(database, link.prefabAsset);
	auto baseIt = base.find(link.prefabLocalFileID);
	if (baseIt == base.end()) {
		return info;
	}

	nlohmann::json instanceComponents;
	world.SerializeEntityComponents(entity, instanceComponents);
	const std::vector<Entity> instanceEntities = CollectInstanceEntities(world, link.prefabInstanceID);
	const PrefabReferenceRemapper::LocalFileIDMap sceneToPrefabLocal =
		BuildSceneToPrefabLocalMap(world, instanceEntities);
	PrefabReferenceRemapper::RemapComponents(
		instanceComponents, sceneToPrefabLocal, PrefabReferenceRemapper::ReferenceSpace::Prefab, link.prefabAsset);
	nlohmann::json baseComponents = baseIt->second.components;
	NormalizeSceneObjectForDiff(baseComponents);
	NormalizeSceneObjectForDiff(instanceComponents);
	const ComponentMapDiff diff = PrefabJsonDiff::DiffComponentMaps(
		baseComponents, instanceComponents, kExcludedDiffTypes);
	for (const auto& [path, value] : diff.modifications) {
		info.modifiedPaths.push_back(path);
	}
	for (const auto& [type, value] : diff.addedComponents) {
		info.addedComponentTypes.push_back(type);
	}
	for (const auto& type : diff.removedComponents) {
		info.removedComponentTypes.push_back(type);
	}
	return info;
}

Engine::Entity Engine::PrefabOverrideUtility::RebuildInstance(ECSWorld& world, AssetDatabase& database,
	HierarchySystem& hierarchySystem, const PrefabInstanceData& data, UUID sceneInstanceID) {

	if (!data.prefabAsset) {
		return Entity::Null();
	}

	// ベースのプレファブを、同一インスタンスIDと保存済みローカルIDの対応付きで展開する
	PrefabSystem prefabSystem{};
	PrefabInstantiateResult result{};
	PrefabInstantiateDesc desc{};
	desc.ownerSceneInstanceID = sceneInstanceID;
	desc.forcedInstanceID = data.instanceID;
	desc.localFileIDRemap = &data.entityMap;
	if (!prefabSystem.InstantiatePrefab(database, hierarchySystem, world, data.prefabAsset, result, desc)) {
		return Entity::Null();
	}
	const PrefabReferenceRemapper::LocalFileIDMap prefabToSceneLocal =
		BuildPrefabToSceneLocalMap(world, result);

	// プレファブ内ローカルIDから生成済みエンティティを引く
	auto findByTarget = [&](UUID target) -> Entity {
		auto it = result.sourceLocalToEntity.find(target);
		return it != result.sourceLocalToEntity.end() ? it->second : Entity::Null();
		};

	// 削除された実体を破棄する
	for (const UUID& target : data.removedEntities) {

		const Entity entity = findByTarget(target);
		if (world.IsAlive(entity)) {
			world.DestroyEntity(entity);
		}
	}
	world.FlushPendingDestroyEntities();
	// ルートが削除済みのインスタンスは子だけを残さず全て破棄する
	if (!world.IsAlive(result.root)) {

		for (const Entity& entity : result.createdEntities) {
			if (world.IsAlive(entity)) {
				world.DestroyEntity(entity);
			}
		}
		world.FlushPendingDestroyEntities();
		return Entity::Null();
	}

	// 削除されたコンポーネントを外す
	for (const auto& removed : data.removedComponents) {

		const Entity entity = findByTarget(removed.target);
		if (world.IsAlive(entity)) {
			world.RemoveComponentByName(entity, removed.type);
		}
	}
	// 追加されたコンポーネントを足す
	for (const auto& added : data.addedComponents) {

		const Entity entity = findByTarget(added.target);
		if (world.IsAlive(entity)) {
			const UUID sceneLocalFileID = added.type == "SceneObject" ? SceneLocalOf(world, entity) : UUID{};
			nlohmann::json value = added.value;
			PrefabReferenceRemapper::RemapComponent(
				added.type, value, prefabToSceneLocal, PrefabReferenceRemapper::ReferenceSpace::Scene, data.prefabAsset);
			world.AddComponentFromJson(entity, added.type, value);
			if (added.type == "SceneObject") {
				RestoreSceneObjectRuntimeFields(world, entity, data.prefabAsset, sceneInstanceID, sceneLocalFileID);
			}
		}
	}

	// プロパティ差分を、対象とコンポーネント単位にまとめてから適用する
	std::unordered_map<UUID, std::unordered_map<std::string, nlohmann::json>> builders;
	for (const auto& mod : data.modifications) {

		const Entity entity = findByTarget(mod.target);
		if (!world.IsAlive(entity)) {
			continue;
		}
		// 経路の先頭セグメントがコンポーネント型名、残りがコンポーネント内のリーフ経路
		const size_t slash = mod.path.find('/');
		const std::string type = (slash == std::string::npos) ? mod.path : mod.path.substr(0, slash);
		const std::string leaf = (slash == std::string::npos) ? std::string{} : mod.path.substr(slash + 1);

		auto& typeMap = builders[mod.target];
		auto builderIt = typeMap.find(type);
		if (builderIt == typeMap.end()) {

			nlohmann::json current;
			world.SerializeComponentToJson(entity, type, current);
			builderIt = typeMap.emplace(type, std::move(current)).first;
		}
		nlohmann::json value = mod.value;
		PrefabReferenceRemapper::RemapValue(
			value, mod.path, prefabToSceneLocal, PrefabReferenceRemapper::ReferenceSpace::Scene, data.prefabAsset);
		PrefabJsonDiff::SetAtPath(builderIt->second, leaf, value);
	}
	for (auto& [target, typeMap] : builders) {

		const Entity entity = findByTarget(target);
		if (!world.IsAlive(entity)) {
			continue;
		}
		for (auto& [type, componentJson] : typeMap) {
			const UUID sceneLocalFileID = type == "SceneObject" ? SceneLocalOf(world, entity) : UUID{};
			world.AddComponentFromJson(entity, type, componentJson);
			if (type == "SceneObject") {
				RestoreSceneObjectRuntimeFields(world, entity, data.prefabAsset, sceneInstanceID, sceneLocalFileID);
			}
		}
	}

	// 追加実体を生成して所属とローカルIDを復元する
	std::vector<Entity> addedEntities;
	for (const auto& added : data.addedEntities) {

		const Entity entity = world.CreateEntity();
		if (added.components.is_object()) {
			for (auto it = added.components.begin(); it != added.components.end(); ++it) {
				world.AddComponentFromJson(entity, it.key(), it.value());
			}
		}
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
		if (added.sceneLocalFileID) {
			sceneObject.localFileID = added.sceneLocalFileID;
		}
		sceneObject.sceneInstanceID = sceneInstanceID;

		// 親への接続はローカルID経由でリンク再構築に任せる
		if (added.parentSceneLocalFileID) {
			if (!world.HasComponent<HierarchyComponent>(entity)) {
				world.AddComponent<HierarchyComponent>(entity);
			}
			world.GetComponent<HierarchyComponent>(entity).parentLocalFileID = added.parentSceneLocalFileID;
		}
		addedEntities.emplace_back(entity);
	}

	// 兄弟順とインスタンス内の親付け替えを適用する
	for (const auto& hierarchyMod : data.hierarchyModifications) {

		const Entity entity = findByTarget(hierarchyMod.target);
		if (!world.IsAlive(entity)) {
			continue;
		}
		if (!world.HasComponent<HierarchyComponent>(entity)) {
			world.AddComponent<HierarchyComponent>(entity);
		}
		auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
		if (hierarchyMod.hasParentOverride) {

			if (hierarchyMod.externalParentSceneLocalFileID) {
				hierarchy.parentLocalFileID = hierarchyMod.externalParentSceneLocalFileID;
			} else if (hierarchyMod.newParentPrefabLocalFileID) {

				const Entity newParent = findByTarget(hierarchyMod.newParentPrefabLocalFileID);
				hierarchy.parentLocalFileID = SceneLocalOf(world, newParent);
			} else {
				hierarchy.parentLocalFileID = UUID{};
			}
		}
		if (hierarchyMod.hasSiblingOrder) {
			hierarchy.siblingOrder = hierarchyMod.siblingOrder;
		}
	}

	// インスタンスと追加実体のランタイムリンクを再構築する
	std::vector<Entity> linkScope;
	linkScope.reserve(result.createdEntities.size() + addedEntities.size());
	for (const Entity& entity : result.createdEntities) {
		if (world.IsAlive(entity)) {
			linkScope.emplace_back(entity);
		}
	}
	for (const Entity& entity : addedEntities) {
		linkScope.emplace_back(entity);
	}
	hierarchySystem.RebuildRuntimeLinks(world, linkScope);

	// ルートの親状態をシーン保存値へ戻す
	if (world.IsAlive(result.root)) {

		hierarchySystem.SetParent(world, result.root, Entity::Null());
		if (data.rootParentSceneLocalFileID) {

			world.GetComponent<HierarchyComponent>(result.root).parentLocalFileID =
				data.rootParentSceneLocalFileID;
			const Entity parent = FindBySceneLocal(world, sceneInstanceID, data.rootParentSceneLocalFileID);
			if (world.IsAlive(parent)) {
				hierarchySystem.SetParent(world, result.root, parent);
			}
		}
	}

	// メッシュのサブメッシュをmesh実体へ正規化する、差分適用でmeshが変わった場合に必要
	for (const Entity& entity : linkScope) {

		if (world.IsAlive(entity) && world.HasComponent<MeshRendererComponent>(entity)) {
			MeshSubMeshAuthoring::SyncComponent(&database, world.GetComponent<MeshRendererComponent>(entity), true);
		}
	}
	return result.root;
}

bool Engine::PrefabOverrideUtility::CanPromoteAddedEntitySubtree(
	ECSWorld& world, const Entity& root, UUID instanceID) {

	if (!world.IsAlive(root) || !instanceID || world.HasComponent<PrefabLinkComponent>(root)) {
		return false;
	}
	const Entity parent = ParentOf(world, root);
	if (!world.IsAlive(parent) || !world.HasComponent<PrefabLinkComponent>(parent) ||
		world.GetComponent<PrefabLinkComponent>(parent).prefabInstanceID != instanceID) {
		return false;
	}

	const std::vector<Entity> subtree = HierarchyUtility::CollectLogicalSubtree(world, root);
	for (const Entity& entity : subtree) {

		if (!world.HasComponent<SceneObjectComponent>(entity) ||
			world.HasComponent<PrefabLinkComponent>(entity) || !SceneLocalOf(world, entity)) {
			return false;
		}
	}
	return !subtree.empty();
}

bool Engine::PrefabOverrideUtility::PromoteAddedEntitySubtrees(nlohmann::json& prefabFileJson,
	ECSWorld& world, AssetID prefabAsset, UUID instanceID, const std::vector<Entity>& roots) {

	if (!prefabAsset || !instanceID || roots.empty() || !prefabFileJson.is_object() ||
		!prefabFileJson.contains("Entities") || !prefabFileJson["Entities"].is_array()) {
		return false;
	}

	std::vector<Entity> addedEntities;
	std::unordered_set<uint64_t> addedEntityKeys;
	for (const Entity& root : roots) {

		if (!CanPromoteAddedEntitySubtree(world, root, instanceID)) {
			return false;
		}
		const std::vector<Entity> subtree = HierarchyUtility::CollectLogicalSubtree(world, root);
		for (const Entity& entity : subtree) {

			const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
			if (addedEntityKeys.insert(key).second) {
				addedEntities.emplace_back(entity);
			}
		}
	}
	if (addedEntities.empty()) {
		return false;
	}

	std::unordered_set<UUID> usedPrefabLocalFileIDs;
	for (const auto& entityJson : prefabFileJson["Entities"]) {

		const std::string localFileID = entityJson.value("LocalFileID", entityJson.value("UUID", ""));
		if (!localFileID.empty()) {
			usedPrefabLocalFileIDs.insert(FromString16Hex(localFileID));
		}
	}

	const std::vector<Entity> instanceEntities = CollectInstanceEntities(world, instanceID);
	PrefabReferenceRemapper::LocalFileIDMap sceneToPrefabLocal =
		BuildSceneToPrefabLocalMap(world, instanceEntities);
	std::unordered_map<uint64_t, UUID> promotedLocalFileIDs;
	for (const Entity& entity : addedEntities) {

		const UUID sceneLocalFileID = SceneLocalOf(world, entity);
		UUID prefabLocalFileID = sceneLocalFileID;
		while (!prefabLocalFileID || usedPrefabLocalFileIDs.contains(prefabLocalFileID)) {
			prefabLocalFileID = UUID::New();
		}
		usedPrefabLocalFileIDs.insert(prefabLocalFileID);
		sceneToPrefabLocal[sceneLocalFileID] = prefabLocalFileID;
		const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
		promotedLocalFileIDs.emplace(key, prefabLocalFileID);
	}

	nlohmann::json promotedEntities = nlohmann::json::array();
	for (const Entity& entity : addedEntities) {

		const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
		const UUID prefabLocalFileID = promotedLocalFileIDs.at(key);

		nlohmann::json components = nlohmann::json::object();
		world.SerializeEntityComponents(entity, components);
		components.erase("PrefabLink");
		if (components.contains("SceneObject") && components["SceneObject"].is_object()) {
			components["SceneObject"]["localFileId"] = ToString(prefabLocalFileID);
		}
		PrefabReferenceRemapper::RemapComponents(
			components, sceneToPrefabLocal, PrefabReferenceRemapper::ReferenceSpace::Prefab, prefabAsset);

		nlohmann::json entityJson = nlohmann::json::object();
		entityJson["LocalFileID"] = ToString(prefabLocalFileID);
		entityJson["Components"] = std::move(components);
		promotedEntities.push_back(std::move(entityJson));
	}
	for (auto& entityJson : promotedEntities) {
		prefabFileJson["Entities"].push_back(std::move(entityJson));
	}

	PrefabSystem prefabSystem{};
	for (const Entity& entity : addedEntities) {

		const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
		prefabSystem.SetPrefabLink(
			world, entity, prefabAsset, promotedLocalFileIDs.at(key), instanceID, false);
	}
	return true;
}

namespace {

	// 破棄→再生成の安全策、インスタンス実体の完全な状態を控えて再生成失敗時に元へ戻せるようにする
	struct InstanceEntityBackup {

		Engine::UUID localFileID{};
		Engine::UUID parentLocalFileID{};
		int32_t siblingOrder = 0;
		nlohmann::json components;
	};

	// 破棄前に各実体のローカルID/親/兄弟順と全コンポーネントを控える
	std::vector<InstanceEntityBackup> CaptureInstanceBackup(Engine::ECSWorld& world, const std::vector<Engine::Entity>& entities) {

		std::vector<InstanceEntityBackup> backup;
		backup.reserve(entities.size());
		for (const Engine::Entity& entity : entities) {

			if (!world.IsAlive(entity)) {
				continue;
			}
			InstanceEntityBackup state{};
			if (world.HasComponent<Engine::SceneObjectComponent>(entity)) {
				state.localFileID = world.GetComponent<Engine::SceneObjectComponent>(entity).localFileID;
			}
			if (world.HasComponent<Engine::HierarchyComponent>(entity)) {
				state.parentLocalFileID = world.GetComponent<Engine::HierarchyComponent>(entity).parentLocalFileID;
				state.siblingOrder = world.GetComponent<Engine::HierarchyComponent>(entity).siblingOrder;
			}
			world.SerializeEntityComponents(entity, state.components);
			backup.emplace_back(std::move(state));
		}
		return backup;
	}

	// 控えた状態から実体を作り直し、ローカルIDと親子付けを復元する
	void RestoreInstanceBackup(Engine::ECSWorld& world, Engine::HierarchySystem& hierarchySystem,
		const std::vector<InstanceEntityBackup>& backup, Engine::UUID sceneInstanceID) {

		std::vector<Engine::Entity> restored;
		restored.reserve(backup.size());
		for (const InstanceEntityBackup& state : backup) {

			const Engine::Entity entity = world.CreateEntity();
			if (state.components.is_object()) {
				for (auto it = state.components.begin(); it != state.components.end(); ++it) {
					world.AddComponentFromJson(entity, it.key(), it.value());
				}
			}
			Engine::SceneAuthoring::EnsureGameObjectDefaults(world, entity);

			// ローカルIDと所属シーン、親子付けは控えた値で確定させる
			auto& sceneObject = world.GetComponent<Engine::SceneObjectComponent>(entity);
			if (state.localFileID) {
				sceneObject.localFileID = state.localFileID;
			}
			sceneObject.sceneInstanceID = sceneInstanceID;
			auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
			hierarchy.parentLocalFileID = state.parentLocalFileID;
			hierarchy.siblingOrder = state.siblingOrder;
			restored.emplace_back(entity);
		}
		hierarchySystem.RebuildRuntimeLinks(world, restored);
	}

	// オーバーライド判定用にベースを正規化する
	// インスタンスは from_json -> 生成時後処理 -> to_json を経るため、生のプレファブJSONと差分が出て誤検出になる
	// ベースも同じ経路で一度通し、インスタンスと同じ表現へ揃えてから比較する
	std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity> NormalizeBaseForDiff(
		Engine::AssetDatabase& database, const std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity>& base) {

		std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity> normalized = base;
		Engine::ECSWorld temp;
		for (auto& [localID, baseEntity] : normalized) {

			const nlohmann::json sourceComponents = baseEntity.components;
			const Engine::Entity entity = temp.CreateEntity();
			if (sourceComponents.is_object()) {
				for (auto it = sourceComponents.begin(); it != sourceComponents.end(); ++it) {
					temp.AddComponentFromJson(entity, it.key(), it.value());
				}
			}
			// 生成時と同じサブメッシュ正規化を通し、インスタンス側の表現に揃える
			if (temp.HasComponent<Engine::MeshRendererComponent>(entity)) {
				Engine::MeshSubMeshAuthoring::SyncComponent(&database, temp.GetComponent<Engine::MeshRendererComponent>(entity), true);
			}
			nlohmann::json normalizedComponents;
			temp.SerializeEntityComponents(entity, normalizedComponents);
			baseEntity.components = std::move(normalizedComponents);
		}
		return normalized;
	}
}

void Engine::PrefabOverrideUtility::PropagateToInstances(ECSWorld& world, AssetDatabase& database,
	HierarchySystem& hierarchySystem, AssetID prefabAsset, const std::unordered_map<UUID, PrefabBaseEntity>& oldBase) {

	if (!prefabAsset) {
		return;
	}

	// 伝播対象のインスタンスごとに、所属シーンとルートを集める
	struct InstanceTarget {

		UUID sceneInstanceID{};
		Entity root = Entity::Null();
	};
	std::unordered_map<UUID, InstanceTarget> targets;
	world.ForEachAliveEntity([&](Entity entity) {

		if (!world.HasComponent<PrefabLinkComponent>(entity)) {
			return;
		}
		const auto& link = world.GetComponent<PrefabLinkComponent>(entity);
		if (link.prefabAsset != prefabAsset) {
			return;
		}
		InstanceTarget& target = targets[link.prefabInstanceID];
		if (world.HasComponent<SceneObjectComponent>(entity)) {
			target.sceneInstanceID = world.GetComponent<SceneObjectComponent>(entity).sceneInstanceID;
		}
		if (link.isPrefabRoot) {
			target.root = entity;
		}
		});

	// 再生成元のプレファブが読めない時は壊さず温存する、再生成失敗でインスタンスを失わないための前段ガード
	const auto prefabFullPath = database.ResolveFullPath(prefabAsset);
	if (prefabFullPath.empty()) {
		return;
	}
	const nlohmann::json prefabProbe = JsonAdapter::Load(prefabFullPath.string(), true);
	if (!prefabProbe.is_object() || !prefabProbe.contains("Entities") ||
		!prefabProbe["Entities"].is_array() || prefabProbe["Entities"].empty()) {
		return;
	}

	// ベースをインスタンスと同じ表現へ正規化し、ラウンドトリップ由来の誤オーバーライド検出を防ぐ
	const std::unordered_map<UUID, PrefabBaseEntity> normalizedBase = NormalizeBaseForDiff(database, oldBase);

	// 各インスタンスを、現在のオーバーライドを保持したまま新しいプレファブで作り直す
	for (auto& [instanceID, target] : targets) {

		const bool hadRoot = world.IsAlive(target.root);
		PrefabInstanceData data = CaptureInstance(world, instanceID, normalizedBase);
		data.prefabAsset = prefabAsset;

		// 旧インスタンスを全メンバーのサブツリーごと破棄する、複数ルートや追加した子も漏らさない
		std::vector<Entity> toDestroy;
		std::unordered_set<uint64_t> collected;
		for (const Entity& member : CollectInstanceEntities(world, instanceID)) {
			const std::vector<Entity> subtree = HierarchyUtility::CollectLogicalSubtree(world, member);
			for (const Entity& entity : subtree) {

				const uint64_t key = (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
				if (collected.insert(key).second) {
					toDestroy.emplace_back(entity);
				}
			}
		}

		// 破棄前に完全な状態を控える、再生成が失敗してもインスタンスを失わないための安全策
		const std::vector<InstanceEntityBackup> backup = CaptureInstanceBackup(world, toDestroy);

		for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {

			if (world.IsAlive(*it)) {
				world.DestroyEntity(*it);
			}
		}
		world.FlushPendingDestroyEntities();

		RebuildInstance(world, database, hierarchySystem, data, target.sceneInstanceID);

		// 有効なルートを持っていたインスタンスの再生成だけ、失敗時に元の状態へ戻す
		if (hadRoot && CollectInstanceEntities(world, instanceID).empty()) {
			RestoreInstanceBackup(world, hierarchySystem, backup, target.sceneInstanceID);
		}
	}
}

//============================================================================
//	プレファブファイルJSON編集ヘルパー
//============================================================================
namespace {

	// プレファブファイルJSON内で指定ローカルIDの実体のComponentsを返す、無ければnullptr
	nlohmann::json* FindPrefabEntityComponents(nlohmann::json& prefabFileJson, Engine::UUID targetLocalFileID) {

		if (!prefabFileJson.is_object() || !prefabFileJson.contains("Entities") ||
			!prefabFileJson["Entities"].is_array()) {
			return nullptr;
		}
		const std::string targetStr = Engine::ToString(targetLocalFileID);
		for (auto& entityJson : prefabFileJson["Entities"]) {

			const std::string localStr = entityJson.value("LocalFileID", entityJson.value("UUID", ""));
			if (localStr != targetStr) {
				continue;
			}
			if (!entityJson.contains("Components") || !entityJson["Components"].is_object()) {
				entityJson["Components"] = nlohmann::json::object();
			}
			return &entityJson["Components"];
		}
		return nullptr;
	}
}

bool Engine::PrefabOverrideUtility::SetPrefabEntityLeaf(nlohmann::json& prefabFileJson, UUID targetLocalFileID,
	const std::string& path, const nlohmann::json& value) {

	nlohmann::json* components = FindPrefabEntityComponents(prefabFileJson, targetLocalFileID);
	if (!components) {
		return false;
	}
	// 経路の先頭セグメントが型名、残りがコンポーネント内のリーフ経路
	const size_t slash = path.find('/');
	const std::string type = (slash == std::string::npos) ? path : path.substr(0, slash);
	const std::string leaf = (slash == std::string::npos) ? std::string{} : path.substr(slash + 1);
	if (!(*components).contains(type) || !(*components)[type].is_object()) {
		(*components)[type] = nlohmann::json::object();
	}
	PrefabJsonDiff::SetAtPath((*components)[type], leaf, value);
	return true;
}

bool Engine::PrefabOverrideUtility::SetPrefabEntityComponent(nlohmann::json& prefabFileJson, UUID targetLocalFileID,
	const std::string& type, const nlohmann::json& value) {

	nlohmann::json* components = FindPrefabEntityComponents(prefabFileJson, targetLocalFileID);
	if (!components) {
		return false;
	}
	(*components)[type] = value;
	return true;
}

bool Engine::PrefabOverrideUtility::RemovePrefabEntityComponent(nlohmann::json& prefabFileJson,
	UUID targetLocalFileID, const std::string& type) {

	nlohmann::json* components = FindPrefabEntityComponents(prefabFileJson, targetLocalFileID);
	if (!components) {
		return false;
	}
	components->erase(type);
	return true;
}

//============================================================================
//	PrefabInstanceData json変換
//============================================================================
namespace {

	// UUIDを文字列へ、空なら空文字列にする
	std::string UUIDToStringOrEmpty(Engine::UUID id) {
		return id ? Engine::ToString(id) : std::string{};
	}
	// 文字列をUUIDへ、空ならゼロにする
	Engine::UUID StringToUUIDOrZero(const std::string& str) {
		return str.empty() ? Engine::UUID{} : Engine::FromString16Hex(str);
	}
}

nlohmann::json Engine::ToJson(const PrefabInstanceData& data) {

	nlohmann::json json = nlohmann::json::object();
	json["PrefabAsset"] = UUIDToStringOrEmpty(data.prefabAsset);
	json["InstanceID"] = UUIDToStringOrEmpty(data.instanceID);
	json["RootParent"] = UUIDToStringOrEmpty(data.rootParentSceneLocalFileID);

	nlohmann::json entityMap = nlohmann::json::array();
	for (const auto& [prefabLocal, sceneLocal] : data.entityMap) {

		nlohmann::json pair = nlohmann::json::object();
		pair["P"] = UUIDToStringOrEmpty(prefabLocal);
		pair["S"] = UUIDToStringOrEmpty(sceneLocal);
		entityMap.push_back(std::move(pair));
	}
	json["EntityMap"] = std::move(entityMap);

	nlohmann::json modifications = nlohmann::json::array();
	for (const auto& mod : data.modifications) {

		nlohmann::json item = nlohmann::json::object();
		item["Target"] = UUIDToStringOrEmpty(mod.target);
		item["Path"] = mod.path;
		item["Value"] = mod.value;
		modifications.push_back(std::move(item));
	}
	json["Modifications"] = std::move(modifications);

	nlohmann::json addedComponents = nlohmann::json::array();
	for (const auto& added : data.addedComponents) {

		nlohmann::json item = nlohmann::json::object();
		item["Target"] = UUIDToStringOrEmpty(added.target);
		item["Type"] = added.type;
		item["Value"] = added.value;
		addedComponents.push_back(std::move(item));
	}
	json["AddedComponents"] = std::move(addedComponents);

	nlohmann::json removedComponents = nlohmann::json::array();
	for (const auto& removed : data.removedComponents) {

		nlohmann::json item = nlohmann::json::object();
		item["Target"] = UUIDToStringOrEmpty(removed.target);
		item["Type"] = removed.type;
		removedComponents.push_back(std::move(item));
	}
	json["RemovedComponents"] = std::move(removedComponents);

	nlohmann::json hierarchyMods = nlohmann::json::array();
	for (const auto& hierarchyMod : data.hierarchyModifications) {

		nlohmann::json item = nlohmann::json::object();
		item["Target"] = UUIDToStringOrEmpty(hierarchyMod.target);
		item["HasParent"] = hierarchyMod.hasParentOverride;
		item["NewParentPrefab"] = UUIDToStringOrEmpty(hierarchyMod.newParentPrefabLocalFileID);
		item["ExternalParent"] = UUIDToStringOrEmpty(hierarchyMod.externalParentSceneLocalFileID);
		item["HasSibling"] = hierarchyMod.hasSiblingOrder;
		item["Sibling"] = hierarchyMod.siblingOrder;
		hierarchyMods.push_back(std::move(item));
	}
	json["HierarchyMods"] = std::move(hierarchyMods);

	nlohmann::json removedEntities = nlohmann::json::array();
	for (const UUID& removed : data.removedEntities) {
		removedEntities.push_back(UUIDToStringOrEmpty(removed));
	}
	json["RemovedEntities"] = std::move(removedEntities);

	nlohmann::json addedEntities = nlohmann::json::array();
	for (const auto& added : data.addedEntities) {

		nlohmann::json item = nlohmann::json::object();
		item["SceneLocalFileID"] = UUIDToStringOrEmpty(added.sceneLocalFileID);
		item["Parent"] = UUIDToStringOrEmpty(added.parentSceneLocalFileID);
		item["Components"] = added.components;
		addedEntities.push_back(std::move(item));
	}
	json["AddedEntities"] = std::move(addedEntities);

	return json;
}

bool Engine::FromJson(const nlohmann::json& json, PrefabInstanceData& data) {

	if (!json.is_object()) {
		return false;
	}

	data = PrefabInstanceData{};
	data.prefabAsset = StringToUUIDOrZero(json.value("PrefabAsset", ""));
	data.instanceID = StringToUUIDOrZero(json.value("InstanceID", ""));
	data.rootParentSceneLocalFileID = StringToUUIDOrZero(json.value("RootParent", ""));

	if (json.contains("EntityMap") && json["EntityMap"].is_array()) {
		for (const auto& pair : json["EntityMap"]) {
			data.entityMap.emplace_back(
				StringToUUIDOrZero(pair.value("P", "")), StringToUUIDOrZero(pair.value("S", "")));
		}
	}
	if (json.contains("Modifications") && json["Modifications"].is_array()) {
		for (const auto& item : json["Modifications"]) {

			PrefabPropertyModification mod{};
			mod.target = StringToUUIDOrZero(item.value("Target", ""));
			mod.path = item.value("Path", "");
			mod.value = item.contains("Value") ? item["Value"] : nlohmann::json{};
			data.modifications.push_back(std::move(mod));
		}
	}
	if (json.contains("AddedComponents") && json["AddedComponents"].is_array()) {
		for (const auto& item : json["AddedComponents"]) {

			PrefabComponentModification added{};
			added.target = StringToUUIDOrZero(item.value("Target", ""));
			added.type = item.value("Type", "");
			added.value = item.contains("Value") ? item["Value"] : nlohmann::json{};
			data.addedComponents.push_back(std::move(added));
		}
	}
	if (json.contains("RemovedComponents") && json["RemovedComponents"].is_array()) {
		for (const auto& item : json["RemovedComponents"]) {

			PrefabComponentModification removed{};
			removed.target = StringToUUIDOrZero(item.value("Target", ""));
			removed.type = item.value("Type", "");
			data.removedComponents.push_back(std::move(removed));
		}
	}
	if (json.contains("HierarchyMods") && json["HierarchyMods"].is_array()) {
		for (const auto& item : json["HierarchyMods"]) {

			PrefabHierarchyModification hierarchyMod{};
			hierarchyMod.target = StringToUUIDOrZero(item.value("Target", ""));
			hierarchyMod.hasParentOverride = item.value("HasParent", false);
			hierarchyMod.newParentPrefabLocalFileID = StringToUUIDOrZero(item.value("NewParentPrefab", ""));
			hierarchyMod.externalParentSceneLocalFileID = StringToUUIDOrZero(item.value("ExternalParent", ""));
			hierarchyMod.hasSiblingOrder = item.value("HasSibling", false);
			hierarchyMod.siblingOrder = item.value("Sibling", 0);
			data.hierarchyModifications.push_back(std::move(hierarchyMod));
		}
	}
	if (json.contains("RemovedEntities") && json["RemovedEntities"].is_array()) {
		for (const auto& item : json["RemovedEntities"]) {
			data.removedEntities.push_back(StringToUUIDOrZero(item.get<std::string>()));
		}
	}
	if (json.contains("AddedEntities") && json["AddedEntities"].is_array()) {
		for (const auto& item : json["AddedEntities"]) {

			PrefabAddedEntity added{};
			added.sceneLocalFileID = StringToUUIDOrZero(item.value("SceneLocalFileID", ""));
			added.parentSceneLocalFileID = StringToUUIDOrZero(item.value("Parent", ""));
			added.components = item.contains("Components") ? item["Components"] : nlohmann::json::object();
			data.addedEntities.push_back(std::move(added));
		}
	}
	return true;
}
