#include "CameraControllerSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/CameraControllerComponent.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/MathLib/Math.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	CameraControllerSystem classMethods
//============================================================================

namespace {

	// 値を0から1の範囲へ収める
	float Clamp01(float value) {

		return (std::clamp)(value, 0.0f, 1.0f);
	}

	// 補間速度とdeltaTimeからフレーム非依存の補間率を作る
	float MakeLerpRate(float speed, float deltaTime) {

		if (speed <= 0.0f) {
			return 1.0f;
		}
		return 1.0f - std::exp(-speed * (std::max)(0.0f, deltaTime));
	}

	// Vector3の各要素を範囲内へ収める
	Engine::Vector3 ClampVector(const Engine::Vector3& value,
		const Engine::Vector3& minValue, const Engine::Vector3& maxValue) {

		return Engine::Vector3(
			(std::clamp)(value.x, minValue.x, maxValue.x),
			(std::clamp)(value.y, minValue.y, maxValue.y),
			(std::clamp)(value.z, minValue.z, maxValue.z));
	}

	// Transformからローカル行列を作る
	Engine::Matrix4x4 MakeLocalMatrix(const Engine::TransformComponent& transform) {

		return Engine::Matrix4x4::MakeAffineMatrix(transform.localScale,
			transform.localRotation, transform.localPos);
	}

	// 現在のTransform値からワールド行列を再帰的に解決する
	Engine::Matrix4x4 ResolveWorldMatrix(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::TransformComponent>(entity)) {
			return Engine::Matrix4x4::Identity();
		}

		const auto& transform = world.GetComponent<Engine::TransformComponent>(entity);
		Engine::Matrix4x4 localMatrix = MakeLocalMatrix(transform);
		if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return localMatrix;
		}

		const Engine::Entity parent = world.GetComponent<Engine::HierarchyComponent>(entity).parent;
		if (!parent.IsValid() || !world.IsAlive(parent)) {
			return localMatrix;
		}
		return localMatrix * ResolveWorldMatrix(world, parent);
	}

	// Entityのワールド位置を取得する
	Engine::Vector3 GetWorldPosition(Engine::ECSWorld& world, const Engine::Entity& entity) {

		return ResolveWorldMatrix(world, entity).GetTranslationValue();
	}

	// ワールド位置をカメラの親空間へ変換する
	Engine::Vector3 WorldToParentLocal(Engine::ECSWorld& world,
		const Engine::Entity& entity, const Engine::Vector3& worldPosition) {

		if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return worldPosition;
		}

		const Engine::Entity parent = world.GetComponent<Engine::HierarchyComponent>(entity).parent;
		if (!parent.IsValid() || !world.IsAlive(parent) || !world.HasComponent<Engine::TransformComponent>(parent)) {
			return worldPosition;
		}
		return Engine::Vector3::Transform(worldPosition,
			Engine::Matrix4x4::Inverse(ResolveWorldMatrix(world, parent)));
	}

	// 追従対象を解決する
	Engine::Entity ResolveTarget(Engine::ECSWorld& world, Engine::UUID target) {

		if (!target) {
			return Engine::Entity::Null();
		}
		return world.FindByUUID(target);
	}

	// 追従処理を実行し、揺れを除いたローカル位置を返す
	Engine::Vector3 ApplyFollow(Engine::ECSWorld& world, const Engine::Entity& entity,
		Engine::TransformComponent& transform, const Engine::CameraFollowSettings& follow, float deltaTime) {

		Engine::Vector3 baseLocalPos = transform.localPos;
		if (!follow.enabled) {
			return baseLocalPos;
		}

		const Engine::Entity target = ResolveTarget(world, follow.target);
		if (!target.IsValid() || !world.IsAlive(target)) {
			return baseLocalPos;
		}

		Engine::Vector3 desiredWorldPos = GetWorldPosition(world, target) + follow.offset;
		if (follow.useBounds) {
			desiredWorldPos = ClampVector(desiredWorldPos, follow.boundsMin, follow.boundsMax);
		}

		Engine::Vector3 desiredLocalPos = WorldToParentLocal(world, entity, desiredWorldPos);
		const Engine::Vector3 axisMask(
			Clamp01(follow.axisMask.x),
			Clamp01(follow.axisMask.y),
			Clamp01(follow.axisMask.z));
		desiredLocalPos = Engine::Vector3::Lerp(baseLocalPos, desiredLocalPos, axisMask);

		return Engine::Vector3::Lerp(baseLocalPos, desiredLocalPos,
			MakeLerpRate(follow.positionLerpSpeed, deltaTime));
	}

	// 注視方向から回転を作成する
	Engine::Quaternion MakeLookAtRotation(const Engine::Vector3& direction, bool lockRoll) {

		Engine::Vector3 forward = direction;
		const float length = forward.Length();
		if (length <= 0.0001f) {
			return Engine::Quaternion::Identity();
		}
		forward /= length;

		const float yaw = std::atan2(forward.x, forward.z);
		const float planarLength = std::sqrt(forward.x * forward.x + forward.z * forward.z);
		const float pitch = std::atan2(-forward.y, planarLength);

		Engine::Vector3 euler = Math::RadToDeg(Engine::Vector3(pitch, yaw, 0.0f));
		if (lockRoll) {
			euler.z = 0.0f;
		}
		return Engine::Quaternion::FromEulerDegrees(euler);
	}

	// 注視処理を実行する
	bool ApplyLookAt(Engine::ECSWorld& world, const Engine::Entity& entity,
		Engine::TransformComponent& transform, const Engine::CameraLookAtSettings& lookAt, float deltaTime) {

		if (!lookAt.enabled) {
			return false;
		}

		const Engine::Entity target = ResolveTarget(world, lookAt.target);
		if (!target.IsValid() || !world.IsAlive(target)) {
			return false;
		}

		const Engine::Vector3 selfWorldPos = GetWorldPosition(world, entity);
		const Engine::Vector3 targetWorldPos = GetWorldPosition(world, target) + lookAt.offset;
		const Engine::Quaternion desiredRotation = MakeLookAtRotation(targetWorldPos - selfWorldPos, lookAt.lockRoll);
		const Engine::Quaternion nextRotation = Engine::Quaternion::Lerp(
			transform.localRotation, desiredRotation, MakeLerpRate(lookAt.rotationLerpSpeed, deltaTime));

		if (Engine::Quaternion::NearlyEqual(transform.localRotation, nextRotation)) {
			return false;
		}

		transform.localRotation = nextRotation;
		return true;
	}

	// 揺れオフセットを作成する
	Engine::Vector3 MakeShakeOffset(const Engine::CameraShakeSettings& shake) {

		if (shake.runtimeDuration <= 0.0f || shake.runtimeAmplitude <= 0.0f) {
			return Engine::Vector3::AnyInit(0.0f);
		}

		const float normalizedTime = Clamp01(shake.runtimeTime / shake.runtimeDuration);
		const float fade = std::pow(1.0f - normalizedTime, (std::max)(0.0f, shake.damping));
		const float phase = shake.runtimeTime * shake.frequency;

		return Engine::Vector3(
			std::sin(phase * 12.9898f),
			std::sin(phase * 78.2330f),
			std::sin(phase * 37.7190f)) * (shake.runtimeAmplitude * fade) * shake.axisMask;
	}

	// 揺れ処理を実行し、最終ローカル位置を返す
	Engine::Vector3 ApplyShake(Engine::CameraShakeSettings& shake,
		const Engine::Vector3& baseLocalPos, float deltaTime) {

		if (!shake.enabled || shake.runtimeDuration <= 0.0f || shake.runtimeAmplitude <= 0.0f) {

			shake.runtimeTime = 0.0f;
			shake.runtimeDuration = 0.0f;
			shake.runtimeAmplitude = 0.0f;
			shake.runtimeLastOffset = Engine::Vector3::AnyInit(0.0f);
			shake.runtimeApplied = false;
			return baseLocalPos;
		}

		shake.runtimeTime += (std::max)(0.0f, deltaTime);
		if (shake.runtimeDuration <= shake.runtimeTime) {

			shake.runtimeTime = 0.0f;
			shake.runtimeDuration = 0.0f;
			shake.runtimeAmplitude = 0.0f;
			shake.runtimeLastOffset = Engine::Vector3::AnyInit(0.0f);
			shake.runtimeApplied = false;
			return baseLocalPos;
		}

		const Engine::Vector3 offset = MakeShakeOffset(shake);
		shake.runtimeLastOffset = offset;
		shake.runtimeApplied = true;
		return baseLocalPos + offset;
	}
}

void Engine::CameraControllerSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// Play中だけゲームカメラ制御を実行する
	if (context.mode != WorldMode::Play) {
		return;
	}

	world.ForEach<CameraControllerComponent, TransformComponent>([&](
		Entity entity, CameraControllerComponent& controller, TransformComponent& transform) {

			if (!controller.enabled || !IsEntityActiveInHierarchy(world, entity)) {
				return;
			}

			// 前フレームの揺れを取り除いた位置を基準にする
			if (controller.shake.runtimeApplied) {
				transform.localPos -= controller.shake.runtimeLastOffset;
			}

			bool changed = false;
			Engine::Vector3 baseLocalPos = transform.localPos;
			if (controller.mode == CameraControlMode::Follow ||
				controller.mode == CameraControlMode::FollowLookAt) {

				baseLocalPos = ApplyFollow(world, entity, transform, controller.follow, context.deltaTime);
			}

			const Engine::Vector3 finalLocalPos = ApplyShake(controller.shake, baseLocalPos, context.deltaTime);
			if (!Engine::Vector3::NearlyEqual(transform.localPos, finalLocalPos)) {

				transform.localPos = finalLocalPos;
				changed = true;
			}

			if (controller.mode == CameraControlMode::LookAt ||
				controller.mode == CameraControlMode::FollowLookAt) {

				changed |= ApplyLookAt(world, entity, transform, controller.lookAt, context.deltaTime);
			}

			if (changed) {
				MarkTransformSubtreeDirty(world, entity);
			}
		});
}
