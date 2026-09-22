#pragma once

//============================================================================
//	include
//============================================================================
#include "CollisionFrameBuilder.h"

namespace Engine::CollisionBodyUtility {

	bool IsDynamicRigidbody(ECSWorld& world, const Entity& entity);

	bool HasRigidbody(ECSWorld& world, const Entity& entity);

	bool IsBoxSurfaceBody(ECSWorld& world, const Entity& entity);
}
