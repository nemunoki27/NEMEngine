#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <cstdint>
#include <string>

namespace Engine {

	//============================================================================
	//	RigidbodyType enum class
	//	剛体の挙動種別
	//============================================================================
	enum class RigidbodyType :
		int32_t {

		Dynamic = 0,
		Kinematic,
		Static,
	};

	//============================================================================
	//	RigidbodyComponent structure
	//	3D物理の剛体で速度と力で運動を表す
	//============================================================================
	struct RigidbodyComponent {

		// 挙動種別
		RigidbodyType bodyType = RigidbodyType::Dynamic;

		// 質量、0以下は積分時に1へ補正する
		float mass = 1.0f;
		// 重力を受けるか
		bool useGravity = true;
		// 重力の倍率
		float gravityScale = 1.0f;
		// 速度の減衰率
		float linearDamping = 0.0f;

		// 軸ごとの移動拘束
		bool freezePositionX = false;
		bool freezePositionY = false;
		bool freezePositionZ = false;

		// 線形速度
		Vector3 linearVelocity = Vector3::AnyInit(0.0f);
		// このステップで適用する蓄積力、保存しない
		Vector3 accumulatedForce = Vector3::AnyInit(0.0f);
	};

	// jsonからコンポーネントへ変換する
	void from_json(const nlohmann::json& in, RigidbodyComponent& component);
	// コンポーネントからjsonへ変換する
	void to_json(nlohmann::json& out, const RigidbodyComponent& component);

	ENGINE_REGISTER_COMPONENT(RigidbodyComponent, "Rigidbody");
} // Engine
