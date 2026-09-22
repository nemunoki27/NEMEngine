#include "PhysicsSystem.h"

//============================================================================
//	include
//============================================================================
#include "RigidbodyIntegration.h"
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

// c++
#include <algorithm>
#include <cmath>

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
		[&](Entity entity, RigidbodyComponent& body, TransformComponent& transform) {

			if (!IsEntityActiveInHierarchy(world, entity)) {
				body.accumulatedForce = Vector3::AnyInit(0.0f);
				body.accumulatedTorque = Vector3::AnyInit(0.0f);
				return;
			}
			// Dynamic以外は積分せず蓄積力とトルクだけ消費する
			if (body.bodyType != RigidbodyType::Dynamic) {
				body.accumulatedForce = Vector3::AnyInit(0.0f);
				body.accumulatedTorque = Vector3::AnyInit(0.0f);
				return;
			}
			RigidbodyIntegration::Integrate(body, transform, dt);
			// localPosとlocalRotationを直接動かすので、TransformSystemへ再計算を促すためdirtyにする
			MarkTransformSubtreeDirty(world, entity);
		});

	// 2D剛体、XY平面のみ動かしZは変えない
	world.ForEach<Rigidbody2DComponent, TransformComponent>(
		[&](Entity entity, Rigidbody2DComponent& body, TransformComponent& transform) {

			if (!IsEntityActiveInHierarchy(world, entity)) {
				body.accumulatedForce = Vector2::AnyInit(0.0f);
				body.accumulatedTorque = 0.0f;
				return;
			}
			if (body.bodyType != RigidbodyType::Dynamic) {
				body.accumulatedForce = Vector2::AnyInit(0.0f);
				body.accumulatedTorque = 0.0f;
				return;
			}
			RigidbodyIntegration::Integrate(body, transform, dt);
			// localPosとlocalRotationを直接動かすので、TransformSystemへ再計算を促すためdirtyにする
			MarkTransformSubtreeDirty(world, entity);
		});
}
