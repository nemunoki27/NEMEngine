#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine {

	//============================================================================
	//	RigidbodyMotion struct
	//	固定ステップのWorld移動量と回転差分
	//============================================================================
	struct RigidbodyMotion {

		// World移動量
		Vector3 translation{};
		// World回転差分
		Quaternion rotationDelta = Quaternion::Identity();
	};

	namespace RigidbodyIntegration {

		// 3D剛体の速度を積分して姿勢差分を返す
		RigidbodyMotion Integrate(RigidbodyComponent& body, float dt);
		// 2D剛体の速度を積分して姿勢差分を返す
		RigidbodyMotion Integrate(Rigidbody2DComponent& body, float dt);
	}
}
