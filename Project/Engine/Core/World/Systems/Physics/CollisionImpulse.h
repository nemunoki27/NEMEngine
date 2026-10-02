#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/Physics/Collision/CollisionDetection.h>

namespace Engine::CollisionImpulse {

	void ResolveContact3D(RigidbodyComponent& body, const Vector3& normal,
		const Vector3& lever, const CollisionShapeInstance& shape);

	void ResolveContact2D(Rigidbody2DComponent& body, const Vector2& normal,
		const Vector2& lever, const CollisionShapeInstance& shape);

	void ResolveContactPair3D(RigidbodyComponent& bodyA, RigidbodyComponent& bodyB,
		const Vector3& normalA, const Vector3& leverA, const Vector3& leverB,
		const CollisionShapeInstance& shapeA, const CollisionShapeInstance& shapeB);

	void ResolveContactPair2D(Rigidbody2DComponent& bodyA, Rigidbody2DComponent& bodyB,
		const Vector2& normalA, const Vector2& leverA, const Vector2& leverB,
		const CollisionShapeInstance& shapeA, const CollisionShapeInstance& shapeB);
}
