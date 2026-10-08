#include "PrefabSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabOwnership.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstantiator.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabSnapshotBuilder.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>

// c++

//============================================================================
//	PrefabSystem classMethods
//============================================================================
void Engine::PrefabSystem::SetPrefabLink(ECSWorld& world, const Entity& entity, AssetID prefabAsset,
	UUID prefabLocalFileID, UUID prefabInstanceID, bool isPrefabRoot,
	UUID ownerPrefabInstanceID, UUID nestedSlotID, bool isPrefabAssetNested) const {

	PrefabOwnership::SetPrefabLink(world, entity, prefabAsset, prefabLocalFileID, prefabInstanceID, isPrefabRoot,
		ownerPrefabInstanceID, nestedSlotID, isPrefabAssetNested);
}

Engine::UUID Engine::PrefabSystem::SetPrefabLinkToSubtree(ECSWorld& world, const Entity& root,
	AssetID prefabAsset, UUID prefabInstanceID) const {

	return PrefabOwnership::SetPrefabLinkToSubtree(world, root, prefabAsset, prefabInstanceID);
}

bool Engine::PrefabSystem::UnpackPrefabInstance(
	ECSWorld& world, const Entity& root, PrefabUnpackMode mode) const {

	return PrefabOwnership::UnpackPrefabInstance(world, root, mode);
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

	return PrefabSnapshotBuilder::SavePrefabFromEntities(database, world, root, entities, prefabAssetPath, prefabInstanceID);
}

bool Engine::PrefabSystem::InstantiatePrefab(AssetDatabase& database, HierarchySystem& hierarchySystem,
	ECSWorld& world, AssetID prefabAsset, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc) const {

	return PrefabInstantiator::InstantiatePrefab(database, hierarchySystem, world, prefabAsset, outResult, desc);
}

bool Engine::PrefabSystem::InstantiatePrefabFromPath(AssetDatabase& database, HierarchySystem& hierarchySystem,
	ECSWorld& world, const std::string& prefabAssetPath, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc) const {

	const AssetID prefabAsset = database.ImportOrGet(prefabAssetPath, AssetType::Prefab);
	return InstantiatePrefab(database, hierarchySystem, world, prefabAsset, outResult, desc);
}
