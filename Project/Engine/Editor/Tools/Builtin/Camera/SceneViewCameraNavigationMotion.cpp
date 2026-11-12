#include "SceneViewCameraNavigation.h"

//============================================================================
//	include
//============================================================================
#include "SceneViewCameraSettings.h"
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine;
using namespace Engine::SceneViewCameraDefaults;

void Engine::SceneViewCameraNavigationUtility::ApplyMotion(SceneViewCameraSettings& settings,
	SceneViewCameraNavigationState& state, const SceneViewCameraNavigationCommand& command, const SceneViewCameraInput& input) {

	if (!command.active) { return; }
	const auto previousCamera = settings.cameraState.transform3D;
	const SceneViewCameraNavigationState previousState = state;
	state.aligning = false;
	const Matrix4x4 rotation = Quaternion::MakeRotateMatrix(GetRotation(settings, state));
	const Vector3 forward = Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f), rotation);
	const Vector3 right = Vector3::TransferNormal(Vector3(1.0f, 0.0f, 0.0f), rotation);
	const Vector3 up = Vector3::TransferNormal(Vector3(0.0f, 1.0f, 0.0f), rotation);
	const float distance = PreparePivot(settings, state);
	const float minDistance = (std::max)(settings.cameraState.perspectiveNearClip * 1.1f, 0.001f);
	auto& camera = settings.cameraState.transform3D;

	// 自由視点では位置を保ち、周回では中心と距離を保つ
	if (command.look || command.orbit) {
		Rotate(settings, state, input.mouseDelta, command.orbit);
	}

	// パンでは注視点も同じ量だけ動かす
	if (command.pan) {
		const float direction = command.preview ? 1.0f : -1.0f;
		const Vector3 offset = (right * input.mouseDelta.x - up * input.mouseDelta.y) * (settings.panSpeed * direction);
		camera.pos += offset;
		state.pivot += offset;
	}

	// キー移動だけ時間差を使い、斜め移動の速度を揃える
	if (command.speedWheel != 0.0f) {
		const float factor = std::exp(std::clamp(command.speedWheel * 0.2f, -20.0f, 20.0f));
		settings.moveSpeed = std::clamp(settings.moveSpeed * factor, kMinMoveSpeed, kMaxMoveSpeed);
	}
	const Vector3 movement = right * command.movement.x + Vector3(0.0f, command.movement.y, 0.0f) + forward * command.movement.z;
	if (movement.Length() > 0.0f && input.deltaTime > 0.0f && std::isfinite(input.deltaTime)) {
		const Vector3 offset = Vector3::NormalizeOr(movement, Vector3::AnyInit(0.0f)) *
			(settings.moveSpeed * input.deltaTime * (command.accelerated ? 4.0f : 1.0f));
		camera.pos += offset;
		state.pivot += offset;
	}

	// プレビューは従来の固定量、SceneViewは注視点までの距離でズームする
	if (command.preview) {
		const Vector3 offset = forward * (command.zoomWheel * settings.zoomRate);
		camera.pos += offset;
		state.pivot += offset;
	} else if (command.zoomWheel != 0.0f || command.zoomDrag) {
		const float steps = command.zoomWheel + (command.zoomDrag ? (input.mouseDelta.x - input.mouseDelta.y) * 0.01f : 0.0f);
		const float exponent = std::clamp(-steps * settings.zoomRate, -20.0f, 20.0f);
		const float nextDistance = std::clamp(distance * std::exp(exponent), minDistance, (std::max)(minDistance, 10000000.0f));
		const Vector3 toward = Vector3::NormalizeOr(state.pivot - camera.pos, forward);
		camera.pos = state.pivot - toward * nextDistance;
		if (settings.cameraState.projectionMode3D == CameraProjectionMode::Orthographic)
			settings.cameraState.orthographicSize3D = std::clamp(
				settings.cameraState.orthographicSize3D * nextDistance / distance, 0.001f, 10000000.0f);
	}

	// 無効な入力では最後の正常なカメラを維持する
	if (!SceneViewCameraSettingsUtility::IsFinite(camera.pos) || !SceneViewCameraSettingsUtility::IsFinite(camera.rotation) ||
		!SceneViewCameraSettingsUtility::IsFinite(state.pivot)) {
		camera = previousCamera;
		state = previousState;
	}
}
