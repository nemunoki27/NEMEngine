#pragma once

//============================================================================
//	include
//============================================================================
#include "CollisionFrameBuilder.h"

namespace Engine::CollisionResponse {

	void ApplyPushback(ECSWorld& world,
		CollisionRuntimeEntity& a, CollisionRuntimeEntity& b, const CollisionContact& contact);
	// ワールド移動量を親座標へ変換して形状も更新する
	void MoveByWorldDelta(ECSWorld& world, CollisionRuntimeEntity& runtime, const Vector3& delta);
}
