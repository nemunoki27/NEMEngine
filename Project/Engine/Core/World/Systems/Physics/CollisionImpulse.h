#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>

namespace Engine::CollisionImpulse {

	void ResolveContact3D(RigidbodyComponent& body, const Vector3& normal, const Vector3& lever);

	void ResolveContact2D(Rigidbody2DComponent& body, const Vector2& normal, const Vector2& lever);
}
