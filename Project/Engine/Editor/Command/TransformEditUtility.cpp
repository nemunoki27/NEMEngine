#include "TransformEditUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>

//============================================================================
//	TransformEditUtility classMethods
//============================================================================

bool Engine::TransformEditUtility::ApplyImmediate(ECSWorld& world,
	const Entity& entity, const TransformComponent& transform) {

	if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
		return false;
	}

	// トランスフォームを即座にワールドに適用する
	auto& dst = world.GetComponent<TransformComponent>(entity);
	dst.localPos = transform.localPos;
	dst.localRotation = transform.localRotation;
	dst.localScale = transform.localScale;
	dst.isDirty = true;
	MarkTransformSubtreeDirty(world, entity);
	return true;
}