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
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Foundation/Math/Math.h>

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

	// 入力でカメラのオービット回転をインクリメンタルに更新する、オイラー非経由
	Quaternion UpdateOrbitRotation(const Quaternion& current, CameraFollowSettings& follow, float deltaTime) {

		Input* input = Input::GetInstance();
		if (!input) {
			return current;
		}

		// 現在操作中のデバイスを判定する、パッドは右スティックそれ以外はマウス移動量を入力にする
		const bool isPad = input->GetType() == InputType::GamePad;

		// 自動設定が有効なら操作中のデバイスに合わせてpad/mouseの有効を切り替える
		if (follow.autoInputDevice) {
			follow.padEnabled = isPad;
			follow.mouseEnabled = !isPad;
		}

		// 現在のデバイスが無効なら入力を0にして回転させない
		const bool deviceEnabled = isPad ? follow.padEnabled : follow.mouseEnabled;
		// パッドスティックは生値(最大32767)なので正規化してから使う
		const Vector2 padInput = input->GetRightStickVal() * (1.0f / input->GetMaxStickValue());
		const Vector2 rawInput = !deviceEnabled
			? Vector2::AnyInit(0.0f)
			: (isPad ? padInput : input->GetMouseMoveValue());

		// 平滑化
		const float lerpT = std::clamp(follow.inputLerpRate * deltaTime, 0.0f, 1.0f);
		follow.smoothedInput = Vector2::Lerp(follow.smoothedInput, rawInput, lerpT);

		// 感度を掛ける、マウスは移動量がフレーム量なのでdtを掛けない
		const Vector2 sensitivity = isPad ? follow.padSensitivity : follow.mouseSensitivity;
		const float dtScale = isPad ? deltaTime : 1.0f;
		const float yawDelta = follow.smoothedInput.x * sensitivity.x * dtScale;
		// マウスは+パッドは-、invertPitchで反転
		const float pitchBaseSign = isPad ? -1.0f : 1.0f;
		const float pitchSign = follow.invertPitch ? -pitchBaseSign : pitchBaseSign;
		const float pitchDelta = follow.smoothedInput.y * sensitivity.y * dtScale * pitchSign;

		// 横回転はワールド上軸まわり、縦回転は横回転後の右軸まわりに合成する
		const Vector3 worldUp(0.0f, 1.0f, 0.0f);
		const Quaternion yawRot = Quaternion::Normalize(Quaternion::MakeAxisAngle(worldUp, yawDelta) * current);
		const Vector3 rightAxis = Vector3::Normalize(
			Vector3::TransferNormal(Vector3(1.0f, 0.0f, 0.0f), Quaternion::MakeRotateMatrix(yawRot)));
		const Quaternion pitchRot = Quaternion::MakeAxisAngle(rightAxis, pitchDelta);
		const Quaternion candidate = Quaternion::Normalize(pitchRot * yawRot);
		return candidate;
	}

	// 追従処理を実行し、揺れを除いたローカル位置を返す、allowInputならオービット回転も行う
	Vector3 ApplyFollow(ECSWorld& world, const Entity& entity, TransformComponent& transform,
		CameraFollowSettings& follow, float deltaTime, bool allowInput) {

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

		// 入力でオービット回転する、回転をカメラへ適用しoffsetも回して対象を中心に回る
		Vector3 offset = follow.offset;
		if (follow.enableInputRotation && allowInput) {

			const Quaternion orbit = UpdateOrbitRotation(transform.localRotation, follow, deltaTime);
			transform.localRotation = orbit;
			offset = Vector3::TransferNormal(follow.offset, Quaternion::MakeRotateMatrix(orbit));
		}

		// 追従対象だけを平滑化し、offsetは平滑化せず即時に足してオービットの遅延を無くす
		const Vector3 targetWorldPos = GetWorldPosition(world, target);
		// 初回はスナップして補間開始点を対象へ合わせる
		if (!follow.targetInitialized) {
			follow.smoothedTarget = targetWorldPos;
			follow.targetInitialized = true;
		}
		follow.smoothedTarget = Vector3::Lerp(follow.smoothedTarget, targetWorldPos, MakeLerpRate(follow.posLerpSpeed, deltaTime));

		// カメラ座標は平滑化ターゲット + 回転済みoffsetで、回転に座標が即時追従する
		Vector3 desiredWorldPos = follow.smoothedTarget + offset;
		Vector3 desiredLocalPos = WorldToParentLocal(world, entity, desiredWorldPos);
		// 軸マスクで座標ごとに追従と固定を切り替える
		return Vector3::Lerp(baseLocalPos, desiredLocalPos, follow.axisMask);
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

			// オービット入力はPlay中だけ受け付ける、Editプレビューでマウスがエディタ操作と干渉しないようにする
			const bool allowInput = isPlay;

			// モードごとに使う設定を選ぶ
			bool changed = false;
			if (controller.mode == CameraControlMode::Follow) {

				transform.localPos = ApplyFollow(world, entity, transform, controller.follow, deltaTime, allowInput);
				changed |= controller.follow.enabled;
			} else if (controller.mode == CameraControlMode::LookAt) {

				changed |= ApplyLookAt(world, entity, transform, controller.lookAt, deltaTime);
			} else if (controller.mode == CameraControlMode::FollowLookAt) {

				// 注視は更新後の位置を使うので追従を先に適用する
				transform.localPos = ApplyFollow(world, entity, transform, controller.followLookAt.follow, deltaTime, allowInput);
				changed |= controller.followLookAt.follow.enabled;
				changed |= ApplyLookAt(world, entity, transform, controller.followLookAt.lookAt, deltaTime);
			}
			// パラメータに変更があればトランスフォームを更新させる
			if (changed) {

				MarkTransformSubtreeDirty(world, entity);
			}
		});
}