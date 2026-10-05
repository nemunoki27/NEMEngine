#include "EditorHierarchyPolicy.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Utility/PrefabInstanceEditUtility.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>

namespace {

	// Scene所属を変更する階層操作は許可しない
	bool HasSameScene(const Engine::ECSWorld& world, Engine::Entity first, Engine::Entity second) {

		const auto* a = world.TryGetComponent<Engine::SceneObjectComponent>(first);
		const auto* b = world.TryGetComponent<Engine::SceneObjectComponent>(second);
		return !a || !b || a->sceneInstanceID == b->sceneInstanceID;
	}
}

bool Engine::EditorHierarchyPolicy::CanSetParent(const EditorContext* context, ECSWorld& world, Entity child, Entity parent) {

	return HierarchyUtility::CanSetParent(world, child, parent) &&
		   PrefabInstanceEditUtility::CanChangeParent(context, world, child, parent) &&
		   (!world.IsAlive(parent) || HasSameScene(world, child, parent));
}

bool Engine::EditorHierarchyPolicy::CanReorder(const EditorContext* context, ECSWorld& world, Entity child, Entity anchor) {

	return world.IsAlive(child) && world.IsAlive(anchor) && child != anchor &&
		   PrefabInstanceEditUtility::CanChangeSiblingOrder(context, world, child, anchor) &&
		   HasSameScene(world, child, anchor) &&
		   HierarchyUtility::GetParent(world, child) == HierarchyUtility::GetParent(world, anchor);
}
