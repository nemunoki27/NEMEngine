#include "SceneViewCameraController.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Input/Input.h>
#include <Engine/MathLib/Math.h>
#include <Engine/MathLib/Matrix4x4.h>
#include <Engine/MathLib/Vector3.h>

//============================================================================
//	SceneViewCameraController classMethods
//============================================================================

namespace {

	// カメラ操作定数
	constexpr float kRotateSpeed = 0.005f;
	constexpr float kZoomRate = 0.4f;
	constexpr float kPanSpeed = 0.02f;

	Engine::Vector3 ToRadians(const Engine::Vector3& deg) {

		return Engine::Vector3(Math::DegToRad(deg.x), Math::DegToRad(deg.y), Math::DegToRad(deg.z));
	}
}

Engine::ManualRenderCameraState Engine::SceneViewCameraController::MakeDefaultState() {

	ManualRenderCameraState state{};

	state.transform3D.pos = Vector3(-6.8f, 2.52f, -8.19f);
	state.transform3D.rotation = Vector3(13.75f, 37.8f, 0.0f);

	state.enableOrthographic = true;
	state.orthoNearClip = 0.0f;
	state.orthoFarClip = 1000.0f;
	state.orthographicCullingMask = -1;

	state.enablePerspective = true;
	state.perspectiveFovY = Math::RadToDeg(0.54f);
	state.perspectiveNearClip = 0.1f;
	state.perspectiveFarClip = 4000.0f;
	state.perspectiveCullingMask = -1;

	return state;
}

void Engine::SceneViewCameraController::Update(ManualRenderCameraState& state, Dimension dimension) {

	// カメラの状態を更新できない場合は処理しない
	if (!CanUpdate()) {
		return;
	}

	switch (dimension) {
	case Engine::Dimension::Type2D:

		Update2D(state);
		break;
	case Engine::Dimension::Type3D:

		Update3D(state);
		break;
	}
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	state;
	dimension;
#endif
}

bool Engine::SceneViewCameraController::CanUpdate() {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	Input* input = Input::GetInstance();
	if (!input) {
		return false;
	}
	// シーンビュー内でマウスが操作されているか
	if (!input->HasViewRect(InputViewArea::Scene)) {
		return false;
	}
	if (!input->IsMouseOnView(InputViewArea::Scene)) {
		return false;
	}
	return true;
#else
	return false;
#endif
}

void Engine::SceneViewCameraController::Update3D(ManualRenderCameraState& state) {

	Input* input = Input::GetInstance();

	// マウスの移動量とホイールの移動量を取得
	const Vector2 mouseDelta = input->GetMouseMoveValue();
	const float wheel = input->GetMouseWheel();

	// 入力がないなら何もしない
	if (!input->PushMouseRight() && !input->PushMouseCenter() && wheel == 0.0f) {
		return;
	}

	// 操作感を合わせるために更新前の回転を使って行列を作る
	Vector3 prevEulerDeg = state.transform3D.rotation;

	Matrix4x4 rotateMatrix = Matrix4x4::MakeRotateMatrix(prevEulerDeg);
	Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(Vector3::AnyInit(1.0f), prevEulerDeg, state.transform3D.pos);

	const float rotateSpeedDeg = Math::RadToDeg(kRotateSpeed);

	// 右ドラッグ: 回転
	if (input->PushMouseRight()) {

		state.transform3D.rotation.x += mouseDelta.y * rotateSpeedDeg;
		state.transform3D.rotation.y += mouseDelta.x * rotateSpeedDeg;
		state.transform3D.rotation = Math::WrapDegree180(state.transform3D.rotation);
	}

	// 中ドラッグ: パン
	if (input->PushMouseCenter()) {

		Vector3 right = { kPanSpeed * mouseDelta.x, 0.0f, 0.0f };
		Vector3 up = { 0.0f, -kPanSpeed * mouseDelta.y, 0.0f };

		right = Vector3::TransferNormal(right, worldMatrix);
		up = Vector3::TransferNormal(up, worldMatrix);

		state.transform3D.pos += right + up;
	}

	// ホイール: 前後移動
	if (wheel != 0.0f) {

		Vector3 forward = { 0.0f, 0.0f, wheel * kZoomRate };
		forward = Vector3::TransferNormal(forward, rotateMatrix);

		state.transform3D.pos += forward;
	}
}

void Engine::SceneViewCameraController::Update2D(ManualRenderCameraState& state) {

	state;
}