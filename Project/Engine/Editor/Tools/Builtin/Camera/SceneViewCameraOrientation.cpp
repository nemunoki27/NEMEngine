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
using namespace Engine::SceneViewCameraNavigationUtility;

namespace {

	// 3D投影の画角から注視面までの倍率を求める
	float GetViewSlope(const SceneViewCameraSettings& settings) {

		return std::tan(Math::DegToRad(std::clamp(settings.cameraState.perspectiveFovY, 1.0f, 179.0f)) * 0.5f);
	}

	// 回転と距離から注視点を向くカメラを更新する
	void ApplyRotation(SceneViewCameraSettings& settings, SceneViewCameraNavigationState& state,
		const Quaternion& rotation, float distance) {

		auto& camera = settings.cameraState.transform3D;
		SetRotation(settings, state, rotation);
		const Vector3 forward = Vector3::TransferNormal(Vector3(0, 0, 1), Quaternion::MakeRotateMatrix(rotation));
		camera.pos = state.pivot - forward * distance;
	}
}

float Engine::SceneViewCameraNavigationUtility::PreparePivot(SceneViewCameraSettings& settings,
	SceneViewCameraNavigationState& state) {

	const auto& camera = settings.cameraState.transform3D;
	const Vector3 forward = Vector3::TransferNormal(Vector3(0, 0, 1), Matrix4x4::MakeRotateMatrix(camera.rotation));
	if (!state.pivotValid || !SceneViewCameraSettingsUtility::IsFinite(state.pivot)) {
		const float distance = settings.cameraState.projectionMode3D == CameraProjectionMode::Orthographic ?
			settings.cameraState.orthographicSize3D / GetViewSlope(settings) : SceneViewCameraDefaults::kDefaultOrbitDistance;
		state.pivot = camera.pos + forward * distance;
		state.pivotValid = true;
	}
	return (std::max)((camera.pos - state.pivot).Length(),
		(std::max)(settings.cameraState.perspectiveNearClip * 1.1f, 0.001f));
}

Quaternion Engine::SceneViewCameraNavigationUtility::GetRotation(const SceneViewCameraSettings& settings,
	SceneViewCameraNavigationState& state) {

	const auto& euler = settings.cameraState.transform3D.rotation;
	if (!state.rotationValid || state.rotationEuler != euler) {
		state.rotation = Quaternion::FromEulerDegrees(euler);
		state.rotationEuler = euler;
		state.rotationValid = true;
	}
	return state.rotation;
}

void Engine::SceneViewCameraNavigationUtility::SetRotation(SceneViewCameraSettings& settings,
	SceneViewCameraNavigationState& state, const Quaternion& rotation) {

	state.rotation = Quaternion::Normalize(rotation);
	state.rotationEuler = Quaternion::ToEulerDegrees(state.rotation);
	state.rotationValid = true;
	settings.cameraState.transform3D.rotation = state.rotationEuler;
}

void Engine::SceneViewCameraNavigationUtility::Rotate(SceneViewCameraSettings& settings,
	SceneViewCameraNavigationState& state, const Vector2& delta, bool orbit) {

	if (!std::isfinite(delta.x) || !std::isfinite(delta.y)) return;
	const float distance = PreparePivot(settings, state);
	const Quaternion yaw = Quaternion::MakeAxisAngle(Vector3(0, 1, 0), delta.x * settings.rotateSpeed);
	const Quaternion current = Quaternion::Normalize(yaw * GetRotation(settings, state));
	const Vector3 right = Vector3::TransferNormal(Vector3(1, 0, 0), Quaternion::MakeRotateMatrix(current));
	const Quaternion pitch = Quaternion::MakeAxisAngle(right, delta.y * settings.rotateSpeed);
	SetRotation(settings, state, pitch * current);
	const Vector3 forward = Vector3::TransferNormal(Vector3(0, 0, 1), Quaternion::MakeRotateMatrix(state.rotation));
	if (orbit) settings.cameraState.transform3D.pos = state.pivot - forward * distance;
	else state.pivot = settings.cameraState.transform3D.pos + forward * distance;
	state.aligning = false;
	state.alignmentAxis = {};
}

void Engine::SceneViewCameraNavigationUtility::ApplyGizmo(SceneViewCameraSettings& settings,
	SceneViewCameraNavigationState& state, const SceneViewCameraGizmoCommand& command) {

	const float distance = PreparePivot(settings, state);
	const Quaternion current = GetRotation(settings, state);
	if (command.action == SceneViewCameraGizmoAction::Align) {
		if (!SceneViewCameraSettingsUtility::IsFinite(command.axis) || command.axis.Length() < 0.5f) return;
		Vector3 forward = -Vector3::Normalize(command.axis);
		const Quaternion previous = state.aligning ? state.alignmentEnd : current;
		const Vector3 previousForward = Vector3::TransferNormal(Vector3(0, 0, 1), Quaternion::MakeRotateMatrix(previous));
		if (std::abs(Vector3::Dot(state.alignmentAxis, Vector3::Normalize(command.axis))) > 0.999f &&
			Vector3::Dot(previousForward, forward) > 0.999f) forward = -forward;
		const Vector3 up = std::abs(forward.y) > 0.99f ? Vector3(0, 0, -forward.y) : Vector3(0, 1, 0);
		state.alignmentStart = current;
		state.alignmentEnd = Quaternion::LookRotation(forward, up);
		state.alignmentElapsed = 0.0f;
		state.alignmentDistance = distance;
		state.aligning = true;
		state.alignmentAxis = Vector3::Normalize(command.axis);
	} else {
		Rotate(settings, state, command.delta, true);
	}
}

void Engine::SceneViewCameraNavigationUtility::UpdateAlignment(SceneViewCameraSettings& settings,
	SceneViewCameraNavigationState& state, float deltaTime) {

	if (!state.aligning || !std::isfinite(deltaTime) || deltaTime <= 0.0f) return;
	state.alignmentElapsed += deltaTime;
	const float progress = std::clamp(state.alignmentElapsed / 0.2f, 0.0f, 1.0f);
	const float amount = progress * progress * (3.0f - 2.0f * progress);
	ApplyRotation(settings, state, Quaternion::Lerp(state.alignmentStart, state.alignmentEnd, amount), state.alignmentDistance);
	if (progress == 1.0f) state.aligning = false;
}
