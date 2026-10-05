#pragma once

//============================================================================
//	include
//============================================================================
#include "CollisionFrameBuilder.h"

namespace Engine::CollisionResponse {

	// 接触の押し戻しと速度を更新する
	void ApplyPushback(ECSWorld& world,
		CollisionRuntimeEntity& a, CollisionRuntimeEntity& b, const CollisionContact& contact);
	// ワールド移動量を親座標へ変換して形状も更新する
	void MoveByWorldDelta(ECSWorld& world, CollisionRuntimeEntity& runtime, const Vector3& delta);
}
