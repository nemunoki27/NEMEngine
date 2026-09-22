#pragma once

//============================================================================
//	include
//============================================================================
#include "CollisionFrameBuilder.h"

namespace Engine::CollisionResponse {

	void ApplyPushback(ECSWorld& world,
		CollisionRuntimeEntity& a, CollisionRuntimeEntity& b, const CollisionContact& contact);
}
