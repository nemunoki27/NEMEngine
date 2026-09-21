#include "PrefabOverrideUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabBaseDocument.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstanceOwnership.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideCapture.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstanceRebuilder.h>
#include <Engine/Core/World/Prefab/Override/PrefabPromotion.h>
#include <Engine/Core/World/Prefab/Override/PrefabPropagation.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabDocumentEditor.h>

std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity> Engine::PrefabOverrideUtility::LoadPrefabBaseEntities(
	AssetDatabase& database, AssetID prefabAsset, UUID* outRootLocalFileID) {

	return PrefabBaseDocument::LoadPrefabBaseEntities(database, prefabAsset, outRootLocalFileID);
}

const std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity>&
Engine::PrefabOverrideUtility::LoadPrefabBaseEntitiesCached(AssetDatabase& database, AssetID prefabAsset) {

	return PrefabBaseDocument::LoadPrefabBaseEntitiesCached(database, prefabAsset);
}

void Engine::PrefabOverrideUtility::InvalidatePrefabBaseCache(AssetID prefabAsset) {

	PrefabBaseDocument::InvalidatePrefabBaseCache(prefabAsset);
}

std::vector<Engine::Entity> Engine::PrefabOverrideUtility::CollectInstanceEntities(ECSWorld& world, UUID instanceID) {

	return PrefabInstanceOwnership::CollectInstanceEntities(world, instanceID);
}

void Engine::PrefabOverrideUtility::SynchronizeNestedPrefabOwnership(ECSWorld& world) {

	PrefabInstanceOwnership::SynchronizeNestedPrefabOwnership(world);
}

Engine::PrefabInstanceData Engine::PrefabOverrideUtility::CaptureInstance(ECSWorld& world, AssetDatabase& database,
	UUID instanceID,
	const std::unordered_map<UUID, PrefabBaseEntity>& base) {

	return PrefabOverrideCapture::CaptureInstance(world, database, instanceID, base);
}

Engine::EntityOverrideInfo Engine::PrefabOverrideUtility::CaptureEntityOverride(
	ECSWorld& world, const Entity& entity, AssetDatabase& database) {

	return PrefabOverrideCapture::CaptureEntityOverride(world, entity, database);
}

Engine::Entity Engine::PrefabOverrideUtility::RebuildInstance(ECSWorld& world, AssetDatabase& database,
	HierarchySystem& hierarchySystem, const PrefabInstanceData& data, UUID sceneInstanceID,
	uint32_t nestedDepth) {

	return PrefabInstanceRebuilder::RebuildInstance(world, database, hierarchySystem, data, sceneInstanceID, nestedDepth);
}

bool Engine::PrefabOverrideUtility::CanPromoteAddedEntitySubtree(
	ECSWorld& world, const Entity& root, UUID instanceID) {

	return PrefabPromotion::CanPromoteAddedEntitySubtree(world, root, instanceID);
}

bool Engine::PrefabOverrideUtility::PromoteAddedEntitySubtrees(nlohmann::json& prefabFileJson,
	ECSWorld& world, AssetID prefabAsset, UUID instanceID, const std::vector<Entity>& roots) {

	return PrefabPromotion::PromoteAddedEntitySubtrees(prefabFileJson, world, prefabAsset, instanceID, roots);
}

bool Engine::PrefabOverrideUtility::PropagateToInstances(ECSWorld& world, AssetDatabase& database,
	HierarchySystem& hierarchySystem, AssetID prefabAsset, const std::unordered_map<UUID, PrefabBaseEntity>& oldBase) {

	return PrefabPropagation::PropagateToInstances(world, database, hierarchySystem, prefabAsset, oldBase);
}

bool Engine::PrefabOverrideUtility::SetPrefabEntityLeaf(nlohmann::json& prefabFileJson, UUID targetLocalFileID,
	const std::string& path, const nlohmann::json& value) {

	return PrefabDocumentEditor::SetPrefabEntityLeaf(prefabFileJson, targetLocalFileID, path, value);
}

bool Engine::PrefabOverrideUtility::SetPrefabEntityComponent(nlohmann::json& prefabFileJson, UUID targetLocalFileID,
	const std::string& type, const nlohmann::json& value) {

	return PrefabDocumentEditor::SetPrefabEntityComponent(prefabFileJson, targetLocalFileID, type, value);
}

bool Engine::PrefabOverrideUtility::RemovePrefabEntityComponent(nlohmann::json& prefabFileJson,
	UUID targetLocalFileID, const std::string& type) {

	return PrefabDocumentEditor::RemovePrefabEntityComponent(prefabFileJson, targetLocalFileID, type);
}
