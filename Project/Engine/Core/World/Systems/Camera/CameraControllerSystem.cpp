#include "CameraControllerSystem.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Camera/CameraControllerComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	CameraControllerSystem classMethods
//============================================================================
namespace {

	// 補間速度とdeltaTimeからフレーム非依存の補間率を作る
	float MakeLerpRate(float speed, float deltaTime) {

		if (speed <= 0.0f) {
			return 1.0f;
		}
		return 1.0f - std::exp(-speed * (std::max)(0.0f, deltaTime));
	}

	// Vector3の各要素を範囲内へ収める
	Vector3 ClampVector(const Vector3& value,
		const Vector3& minValue, const Vector3& maxValue) {

		return Vector3((std::clamp)(value.x, minValue.x, maxValue.x),
			(std::clamp)(value.y, minValue.y, maxValue.y),
			(std::clamp)(value.z, minValue.z, maxValue.z));
	}

	// 現在のTransform値からワールド行列を再帰的に解決する
	Matrix4x4 ResolveWorldMatrix(ECSWorld& world, const Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
			return Matrix4x4::Identity();
		}

		const auto& transform = world.GetComponent<TransformComponent>(entity);
		Matrix4x4 localMatrix = MakeLocalMatrix(transform);
		if (!world.HasComponent<HierarchyComponent>(entity)) {
			return localMatrix;
		}

		const Entity parent = world.GetComponent<HierarchyComponent>(entity).parent;
		if (!parent.IsValid() || !world.IsAlive(parent)) {
			return localMatrix;
		}
		return localMatrix * ResolveWorldMatrix(world, parent);
	}

	// Entityのワールド位置を取得する
	Vector3 GetWorldPosition(ECSWorld& world, const Entity& entity) {

		return ResolveWorldMatrix(world, entity).GetTranslationValue();
	}

	// ワールド位置をカメラの親空間へ変換する
	Vector3 WorldToParentLocal(ECSWorld& world,
		const Entity& entity, const Vector3& worldPosition) {

		if (!world.HasComponent<HierarchyComponent>(entity)) {
			return worldPosition;
		}

		const Entity parent = world.GetComponent<HierarchyComponent>(entity).parent;
		if (!parent.IsValid() || !world.IsAlive(parent) || !world.HasComponent<TransformComponent>(parent)) {
			return worldPosition;
		}
		return Vector3::Transform(worldPosition, Matrix4x4::Inverse(ResolveWorldMatrix(world, parent)));
	}

	// 追従処理を実行し、揺れを除いたローカル位置を返す
	Vector3 ApplyFollow(ECSWorld& world, const Entity& entity, TransformComponent& transform, const CameraFollowSettings& follow, float deltaTime) {

		Vector3 baseLocalPos = transform.localPos;
		// 追従が無効ならそのまま座標を返す
		if (!follow.enabled) {
			return baseLocalPos;
		}

		// 追従対象のエンティティを取得
		const Entity target = SceneObjectUtility::FindByLocalFileID(world, follow.target);
		if (!world.IsAlive(target)) {
			return baseLocalPos;
		}
		// 追従先の座標を取得
		Vector3 desiredWorldPos = GetWorldPosition(world, target) + follow.offset;
		Vector3 desiredLocalPos = WorldToParentLocal(world, entity, desiredWorldPos);
		// 軸マスクで補間率を座標ごとに調整
		desiredLocalPos = Vector3::Lerp(baseLocalPos, desiredLocalPos, follow.axisMask);

		// 追従先補間
		return Vector3::Lerp(baseLocalPos, desiredLocalPos, MakeLerpRate(follow.posLerpSpeed, deltaTime));
	}

	// 注視処理を実行する
	bool ApplyLookAt(ECSWorld& world, const Entity& entity, TransformComponent& transform, const CameraLookAtSettings& lookAt, float deltaTime) {

		if (!lookAt.enabled) {
			return false;
		}

		// 追従対象のエンティティを取得
		const Entity target = SceneObjectUtility::FindByLocalFileID(world, lookAt.target);
		if (!world.IsAlive(target)) {
			return false;
		}

		// 追従先の座標、回転を取得
		Vector3 targetWorldPos = GetWorldPosition(world, target) + lookAt.offset;
		// ジンバルロック回避のためオイラーを介さずQuaternionで注視回転を作る
		Quaternion desiredRotation = Quaternion::LookRotation(targetWorldPos - GetWorldPosition(world, entity), Vector3(0.0f, 1.0f, 0.0f));
		// 回転補間
		transform.localRotation = Quaternion::Lerp(transform.localRotation, desiredRotation, MakeLerpRate(lookAt.rotationLerpSpeed, deltaTime));

		return true;
	}
}

void CameraControllerSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// ゲーム再生中か
	bool isPlay = context.mode == WorldMode::Play;
	// 状態に応じたdeltaTimeを取得
	float deltaTime = isPlay ? context.deltaTime : context.unscaledDeltaTime;
	world.ForEach<CameraControllerComponent, TransformComponent>([&](
		Entity entity, CameraControllerComponent& controller, TransformComponent& transform) {

			// 無効再生中なら処理しない
			if (!controller.enabled || (!isPlay && !controller.editorPreview)) {
				return;
			}
			// エンティティが有効か
			if (!IsEntityActiveInHierarchy(world, entity)) {
				return;
			}

			// モードごとに使う設定を選ぶ
			bool changed = false;
			if (controller.mode == CameraControlMode::Follow) {

				transform.localPos = ApplyFollow(world, entity, transform, controller.follow, deltaTime);
				changed |= controller.follow.enabled;
			} else if (controller.mode == CameraControlMode::LookAt) {

				changed |= ApplyLookAt(world, entity, transform, controller.lookAt, deltaTime);
			} else if (controller.mode == CameraControlMode::FollowLookAt) {

				// 注視は更新後の位置を使うので追従を先に適用する
				transform.localPos = ApplyFollow(world, entity, transform, controller.followLookAt.follow, deltaTime);
				changed |= controller.followLookAt.follow.enabled;
				changed |= ApplyLookAt(world, entity, transform, controller.followLookAt.lookAt, deltaTime);
			}
			// パラメータに変更があればトランスフォームを更新させる
			if (changed) {

				MarkTransformSubtreeDirty(world, entity);
			}
		});
}