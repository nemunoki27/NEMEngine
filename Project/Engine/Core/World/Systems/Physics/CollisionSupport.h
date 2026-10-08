#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionDetection.h>

namespace Engine {
	class ECSWorld;
	struct TransformComponent;
	struct RigidbodyComponent;
}

namespace Engine::CollisionSupport {

	// 支持面へ形状の姿勢を揃える
	void SettleSupportedRotation(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::Vector3& normal,
		const Engine::CollisionShapeInstance* selfShape);
	// 3Dの重心と支持面の関係を判定する
	bool IsCenterSupported3D(const Engine::TransformComponent& transform, const Engine::Vector3& centerOfMass,
		const Engine::Vector3& normal, const Engine::CollisionShapeInstance* selfShape,
		const Engine::CollisionShapeInstance* supportShape);
	// 2Dの重心と支持面の関係を判定する
	bool IsCenterSupported2D(const Engine::Vector3& centerOfMass, const Engine::Vector3& normal,
		const Engine::CollisionShapeInstance* selfShape, const Engine::CollisionShapeInstance* supportShape);
	// 支持面での角速度を安定させる
	void StabilizeSupportedRotation(Engine::RigidbodyComponent& body, const Engine::Vector3& normal);
}
