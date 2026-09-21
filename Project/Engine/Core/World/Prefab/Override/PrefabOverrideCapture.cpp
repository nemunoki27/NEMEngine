#include "PrefabOverrideCapture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabJsonDiff.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstanceUtility.h>

// c++
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace Engine::PrefabInstanceUtility;

namespace {

	const std::vector<std::string> kExcludedDiffTypes = { "PrefabLink", "Hierarchy" };
}

Engine::PrefabInstanceData Engine::PrefabOverrideCapture::CaptureInstance(ECSWorld& world, AssetDatabase& database,
	UUID instanceID,
	const std::unordered_map<UUID, PrefabBaseEntity>& base) {

	PrefabInstanceData data{};
	data.instanceID = instanceID;

	// インスタンスに属するエンティティを集め、プレファブ内ローカルIDから引けるようにする
	const std::vector<Entity> instanceEntities = PrefabOverrideUtility::CollectInstanceEntities(world, instanceID);
	std::unordered_map<UUID, Entity> instanceByPrefabLocal;
	for (const Entity& entity : instanceEntities) {

		const auto& link = world.GetComponent<PrefabLinkComponent>(entity);
		instanceByPrefabLocal.emplace(link.prefabLocalFileID, entity);
		data.prefabAsset = link.prefabAsset;
		if (link.isPrefabRoot) {
			data.ownerPrefabInstanceID = link.ownerPrefabInstanceID;
			data.nestedSlotID = link.nestedSlotID;
			data.isPrefabAssetNested = link.isPrefabAssetNested;
		}
	}
	const PrefabReferenceRemapper::LocalFileIDMap sceneToPrefabLocal =
		BuildSceneToPrefabLocalMap(world, instanceEntities);

	// 各インスタンスエンティティの差分を抽出する
	for (const Entity& entity : instanceEntities) {

		const auto& link = world.GetComponent<PrefabLinkComponent>(entity);
		const UUID localID = link.prefabLocalFileID;
		data.entityMap.emplace_back(localID, SceneLocalOf(world, entity));
		data.stableUUIDMap.emplace_back(localID, world.GetUUID(entity));

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

	// 親Prefabが所有する別Prefabを差分のまま再帰保存する
	std::vector<Entity> nestedRoots;
	world.ForEachAliveEntity([&](Entity entity) {

		if (!world.HasComponent<PrefabLinkComponent>(entity)) {
			return;
		}
		const auto& link = world.GetComponent<PrefabLinkComponent>(entity);
		if (link.isPrefabRoot && link.ownerPrefabInstanceID == instanceID &&
			link.prefabInstanceID != instanceID) {
			nestedRoots.emplace_back(entity);
		}
		});
	std::sort(nestedRoots.begin(), nestedRoots.end(), [&](const Entity& lhs, const Entity& rhs) {
		return world.GetComponent<PrefabLinkComponent>(lhs).nestedSlotID.value <
			world.GetComponent<PrefabLinkComponent>(rhs).nestedSlotID.value;
		});
	for (const Entity& nestedRoot : nestedRoots) {

		const auto& nestedLink = world.GetComponent<PrefabLinkComponent>(nestedRoot);
		const auto nestedBase = PrefabOverrideUtility::LoadPrefabBaseEntities(database, nestedLink.prefabAsset);
		if (nestedBase.empty()) {
			continue;
		}
		data.nestedInstances.emplace_back(
			CaptureInstance(world, database, nestedLink.prefabInstanceID, nestedBase));
	}

	// 親Prefabアセットに存在するがシーンから消えたネストスロットを削除差分として記録する
	std::unordered_set<UUID> existingNestedSlots;
	for (const auto& nested : data.nestedInstances) {
		existingNestedSlots.insert(nested.nestedSlotID);
	}
	const auto prefabPath = database.ResolveFullPath(data.prefabAsset);
	const nlohmann::json prefabJson = prefabPath.empty() ?
		nlohmann::json{} : JsonAdapter::Load(prefabPath);
	if (prefabJson.is_object() && prefabJson.contains("NestedPrefabInstances") &&
		prefabJson["NestedPrefabInstances"].is_array()) {

		for (const auto& nestedJson : prefabJson["NestedPrefabInstances"]) {

			PrefabInstanceData nested{};
			if (FromJson(nestedJson, nested) && nested.nestedSlotID &&
				!existingNestedSlots.contains(nested.nestedSlotID)) {

				data.removedNestedSlots.emplace_back(nested.nestedSlotID);
			}
		}
	}
	return data;
}

Engine::EntityOverrideInfo Engine::PrefabOverrideCapture::CaptureEntityOverride(
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
	const auto base = PrefabOverrideUtility::LoadPrefabBaseEntities(database, link.prefabAsset);
	auto baseIt = base.find(link.prefabLocalFileID);
	if (baseIt == base.end()) {
		return info;
	}

	nlohmann::json instanceComponents;
	world.SerializeEntityComponents(entity, instanceComponents);
	const std::vector<Entity> instanceEntities = PrefabOverrideUtility::CollectInstanceEntities(world, link.prefabInstanceID);
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
