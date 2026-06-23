#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

namespace Engine {

	//============================================================================
	//	Rigidbody2DComponent structure
	//	2D物理の剛体でXY平面の速度と力で運動を表す
	//============================================================================
	struct Rigidbody2DComponent {

		// 挙動種別、3Dと同じ種別を使う
		RigidbodyType bodyType = RigidbodyType::Dynamic;

		// 質量、0以下は積分時に1へ補正する
		float mass = 1.0f;
		// 重力を受けるか
		bool useGravity = true;
		// 重力の倍率
		float gravityScale = 1.0f;
		// 速度の減衰率
		float linearDamping = 0.0f;
		// はね返り係数、0で跳ねず1で完全反発
		float restitution = 0.0f;
		// 接線方向の摩擦、0で滑り続け1で即止まる
		float friction = 0.4f;
		// 角速度の減衰率
		float angularDamping = 0.05f;
		// Z軸まわりの角速度、rad/s
		float angularVelocity = 0.0f;

		// 軸ごとの移動拘束
		bool freezePositionX = false;
		bool freezePositionY = false;
		// 回転を固定するか
		bool freezeRotation = false;
		// 支えが重心からずれたとき倒れて落ちるか
		bool allowTopple = false;

		// 線形速度
		Vector2 linearVelocity = Vector2::AnyInit(0.0f);
		// このステップで適用する蓄積力、保存しない
		Vector2 accumulatedForce = Vector2::AnyInit(0.0f);
		// このステップで適用する蓄積トルク、Z軸まわり、保存しない
		float accumulatedTorque = 0.0f;
	};

	// jsonからコンポーネントへ変換する
	void from_json(const nlohmann::json& in, Rigidbody2DComponent& component);
	// コンポーネントからjsonへ変換する
	void to_json(nlohmann::json& out, const Rigidbody2DComponent& component);

	ENGINE_REGISTER_COMPONENT(Rigidbody2DComponent, "Rigidbody2D");
} // Engine
