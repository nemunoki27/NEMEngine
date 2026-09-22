#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

namespace Engine::RigidbodyIntegration {

	// 3D剛体の速度と姿勢を積分
	void Integrate(RigidbodyComponent& body, TransformComponent& transform, float dt);
	// 2D剛体の速度と姿勢を積分
	void Integrate(Rigidbody2DComponent& body, TransformComponent& transform, float dt);
}
