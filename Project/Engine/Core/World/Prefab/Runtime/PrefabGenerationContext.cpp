#include "PrefabGenerationContext.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideTypes.h>

//============================================================================
//	PrefabGenerationContext structMethods
//============================================================================
Engine::PrefabGenerationContext::PrefabGenerationContext(AssetDatabase& database,
	HierarchySystem& hierarchySystem, ECSWorld& world) :
	database(database), hierarchySystem(hierarchySystem), world(world) {

	// ネスト全体で既存IDの走査を一度にまとめる
	world.ForEach<SceneObjectComponent>([&](Entity, const SceneObjectComponent& object) {
		ReserveLocalFileID(object.localFileID);
	});
}

Engine::UUID Engine::PrefabGenerationContext::AllocateLocalFileID() {

	while (true) {
		const UUID candidate = UUID::New();
		if (reservedLocalFileIDs_.insert(candidate).second) {
			return candidate;
		}
	}
}

void Engine::PrefabGenerationContext::ReserveLocalFileID(UUID localFileID) {

	if (localFileID) {
		reservedLocalFileIDs_.insert(localFileID);
	}
}

void Engine::PrefabGenerationContext::ReserveInstanceLocalFileIDs(const PrefabInstanceData& data) {

	for (const auto& entry : data.entityMap) {
		ReserveLocalFileID(entry.second);
	}
	for (const auto& added : data.addedEntities) {
		ReserveLocalFileID(added.sceneLocalFileID);
	}
	for (const auto& nested : data.nestedInstances) {
		ReserveInstanceLocalFileIDs(nested);
	}
}
