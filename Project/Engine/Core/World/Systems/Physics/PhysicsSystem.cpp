#include "PhysicsSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

// c++
#include <algorithm>

namespace {

	// 既定重力
	const Engine::Vector3 kGravity = Engine::Vector3(0.0f, -9.81f, 0.0f);

	// 力と重力を速度へ反映して減衰させる、2Dと3Dで共通
	template <typename Vec>
	void IntegrateVelocity(Vec& velocity, const Vec& force, const Vec& gravityStep, float mass, float damping, float dt) {

		velocity += force * (dt / mass);
		velocity += gravityStep;
		velocity *= std::clamp(1.0f - damping * dt, 0.0f, 1.0f);
	}
}

void Engine::PhysicsSystem::FixedUpdate(ECSWorld& world, SystemContext& context) {

	// 物理はPlay中のみ進める
	if (context.mode != WorldMode::Play) {
		return;
	}
	const float dt = context.fixedDeltaTime;
	if (dt <= 0.0f) {
		return;
	}

	// 3D剛体
	world.ForEach<RigidbodyComponent, TransformComponent>(
		[&]([[maybe_unused]] Entity entity, RigidbodyComponent& body, TransformComponent& transform) {

			// Dynamic以外は積分せず蓄積力だけ消費する
			if (body.bodyType != RigidbodyType::Dynamic) {
				body.accumulatedForce = Vector3::AnyInit(0.0f);
				return;
			}
			const float mass = body.mass > 0.0f ? body.mass : 1.0f;
			const Vector3 gravityStep = body.useGravity ? kGravity * (body.gravityScale * dt) : Vector3::AnyInit(0.0f);
			IntegrateVelocity(body.linearVelocity, body.accumulatedForce, gravityStep, mass, body.linearDamping, dt);

			// 拘束軸の速度を止める
			if (body.freezePositionX) { body.linearVelocity.x = 0.0f; }
			if (body.freezePositionY) { body.linearVelocity.y = 0.0f; }
			if (body.freezePositionZ) { body.linearVelocity.z = 0.0f; }

			// 位置を更新して蓄積力を消費する
			transform.localPos += body.linearVelocity * dt;
			body.accumulatedForce = Vector3::AnyInit(0.0f);
		});

	// 2D剛体、XY平面のみ動かしZは変えない
	world.ForEach<Rigidbody2DComponent, TransformComponent>(
		[&]([[maybe_unused]] Entity entity, Rigidbody2DComponent& body, TransformComponent& transform) {

			if (body.bodyType != RigidbodyType::Dynamic) {
				body.accumulatedForce = Vector2::AnyInit(0.0f);
				return;
			}
			const float mass = body.mass > 0.0f ? body.mass : 1.0f;
			const Vector2 gravityStep = body.useGravity ? Vector2(kGravity.x, kGravity.y) * (body.gravityScale * dt) : Vector2::AnyInit(0.0f);
			IntegrateVelocity(body.linearVelocity, body.accumulatedForce, gravityStep, mass, body.linearDamping, dt);

			if (body.freezePositionX) { body.linearVelocity.x = 0.0f; }
			if (body.freezePositionY) { body.linearVelocity.y = 0.0f; }

			transform.localPos.x += body.linearVelocity.x * dt;
			transform.localPos.y += body.linearVelocity.y * dt;
			body.accumulatedForce = Vector2::AnyInit(0.0f);
		});
}
