#pragma once

//============================================================================
//	include
//============================================================================
#include "CollisionFrameBuilder.h"

namespace Engine::CollisionBodyUtility {

	// 動的な剛体か
	bool IsDynamicRigidbody(ECSWorld& world, const Entity& entity);

	// 剛体を持つか
	bool HasRigidbody(ECSWorld& world, const Entity& entity);

	// 箱の支持面として扱うか
	bool IsBoxSurfaceBody(ECSWorld& world, const Entity& entity);

	// 固定軸を移動量へ反映する
	Engine::Vector3 ApplyTranslationConstraints(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::Vector3 value);
	// 変更した姿勢をWorld行列へ反映する
	void UpdateTransformWorldMatrix(
		Engine::ECSWorld& world, const Engine::Entity& entity, Engine::TransformComponent& transform);
	// Worldの移動量をEntityへ適用する
	void MoveEntity(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::Vector3& delta);
	// 接触応答に使う逆質量を求める
	float ResolveInverseMass(Engine::ECSWorld& world, Engine::Entity entity);
}
