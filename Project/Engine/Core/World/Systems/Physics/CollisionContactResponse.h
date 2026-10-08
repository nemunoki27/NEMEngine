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

namespace Engine::CollisionContactResponse {

	// 単体の接触速度を解く
	void ResolveContactVelocity(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::Vector3& pushOutDir,
		const Engine::Vector3& contactPoint, const Engine::CollisionShapeInstance* selfShape,
		const Engine::CollisionShapeInstance* supportShape);
	// 二つの剛体の接触速度を解く
	bool ResolvePairContactVelocity(Engine::ECSWorld& world, const Engine::Entity& entityA, const Engine::Entity& entityB,
		const Engine::Vector3& normalA, const Engine::Vector3& contactPoint, const Engine::CollisionShapeInstance* shapeA,
		const Engine::CollisionShapeInstance* shapeB);
}
