#include "EntityTreeSnapshot.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>

//============================================================================
//	EntitySnapshotUtility namespaceMethods
//============================================================================
void Engine::EntitySnapshotUtility::CaptureReferenceTargets(ECSWorld& world, EntityTreeSnapshot& snapshot) {

	const PrefabReferenceRemapper::LocalFileIDMap unchanged;
	for (auto& saved : snapshot.entities) {
		std::vector<EntityReferenceTargetSnapshot> targets;
		for (auto& [type, component] : saved.components.items()) {
			PrefabReferenceRemapper::RemapComponentReferences(type, component, unchanged, [&](nlohmann::json& reference) {
				AssetID asset;
				UUID localID;
				if (!PrefabReferenceRemapper::TryReadEntityReference(reference, asset, localID)) {
					return;
				}
				const Entity target = asset ? SceneObjectUtility::ResolveReference(world, asset, localID, saved.sceneInstanceID) :
					SceneObjectUtility::FindByLocalFileID(world, saved.sceneInstanceID, localID);
				// 未解決も記録し、別Sceneの同じ番号で補わない
				targets.push_back({ asset, localID, world.IsAlive(target) ? world.GetUUID(target) : UUID{} });
			});
		}
		saved.referenceTargets = std::move(targets);
		saved.referencesCaptured = true;
	}
}
