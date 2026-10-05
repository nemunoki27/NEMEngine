#include "PhysicsSystem.h"

//============================================================================
//	include
//============================================================================
#include "RigidbodyIntegration.h"
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

namespace {

	// Worldの積分結果を親追従座標へ適用する
	template <typename Body>
	void IntegrateBody(Engine::ECSWorld& world, Engine::Entity entity, Body& body,
		Engine::TransformComponent& transform, float dt) {

		// 現在の親姿勢からCCDの開始位置を求める
		Engine::ResolvedWorldTransform parentFollow{};
		const bool resolved = Engine::TransformWorldUtility::ResolveParentFollowTransform(world, entity, parentFollow);
		if (resolved) {
			body.previousWorldPosition = (Engine::MakeLocalMatrix(transform) * parentFollow.matrix).GetTranslationValue();
		}
		body.hasPreviousWorldPosition = resolved;

		// 親座標へ戻せなくても蓄積力は一度だけ消費する
		const Engine::RigidbodyMotion motion = Engine::RigidbodyIntegration::Integrate(body, dt);
		Engine::Matrix4x4 inverseParent{};
		Engine::Quaternion inverseParentRotation{};
		if (!resolved || !Engine::Matrix4x4::TryInverse(parentFollow.matrix, inverseParent) ||
			!Engine::Quaternion::TryInverse(parentFollow.rotation, inverseParentRotation)) {
			return;
		}

		// Worldの移動量と回転差分をlocal値へ戻す
		transform.localPos += Engine::Vector3::TransferNormal(motion.translation, inverseParent);
		if (motion.rotationDelta != Engine::Quaternion::Identity()) {
			transform.localRotation = Engine::Quaternion::Normalize(
				inverseParentRotation * motion.rotationDelta * parentFollow.rotation * transform.localRotation);
		}
		Engine::MarkTransformSubtreeDirty(world, entity);
		transform.worldMatrix = Engine::MakeLocalMatrix(transform) * parentFollow.matrix;
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
			IntegrateBody(world, entity, body, transform, dt);
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
			IntegrateBody(world, entity, body, transform, dt);
		});
}
