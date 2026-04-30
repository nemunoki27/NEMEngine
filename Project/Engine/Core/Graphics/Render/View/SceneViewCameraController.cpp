#include "SceneViewCameraController.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Input/Input.h>
#include <Engine/MathLib/Math.h>
#include <Engine/MathLib/Matrix4x4.h>
#include <Engine/MathLib/Vector3.h>
#include <Engine/Core/Runtime/RuntimePaths.h>
#include <Engine/Utility/Json/JsonAdapter.h>

//============================================================================
//	SceneViewCameraController classMethods
//============================================================================

namespace {

	// カメラ操作定数
	constexpr float kRotateSpeed = 0.005f;
	constexpr float kZoomRate = 0.4f;
	constexpr float kPanSpeed = 0.02f;

	// カメラ保存パス
	const std::string kCameraJsonPath = "Config/initExeData.exeConfig.json";
}

Engine::SceneViewCameraController::~SceneViewCameraController() {

	// カメラを閉じた瞬間の状態を保存する
	nlohmann::json data{};

	// 2D(今は未使用)
	{
	}
	// 3D
	{
		data["transform3D.pos"] = cameraState_.transform3D.pos.ToJson();
		data["transform3D.rotation"] = cameraState_.transform3D.rotation.ToJson();
		data["perspectiveFovY"] = cameraState_.perspectiveFovY;
		data["perspectiveNearClip"] = cameraState_.perspectiveNearClip;
		data["perspectiveFarClip"] = cameraState_.perspectiveFarClip;
		data["perspectiveCullingMask"] = cameraState_.perspectiveCullingMask;
	}

	JsonAdapter::Save(RuntimePaths::GetEngineAssetPath(kCameraJsonPath).string(), data);
}

void Engine::SceneViewCameraController::MakeDefaultState() {

	cameraState_ = {};

	cameraState_.transform3D.pos = Vector3(-6.8f, 2.52f, -8.19f);
	cameraState_.transform3D.rotation = Vector3(13.75f, 37.8f, 0.0f);

	cameraState_.enableOrthographic = true;
	cameraState_.orthoNearClip = 0.0f;
	cameraState_.orthoFarClip = 1000.0f;
	cameraState_.orthographicCullingMask = -1;

	cameraState_.enablePerspective = true;
	cameraState_.perspectiveFovY = Math::RadToDeg(0.54f);
	cameraState_.perspectiveNearClip = 0.1f;
	cameraState_.perspectiveFarClip = 4000.0f;
	cameraState_.perspectiveCullingMask = -1;

	// 保存したカメラデータがあれば読みこんで設定する
	if (!JsonAdapter::Check(RuntimePaths::GetEngineAssetPath(kCameraJsonPath).string())) {
		return;
	}
	nlohmann::json data = JsonAdapter::Load(RuntimePaths::GetEngineAssetPath(kCameraJsonPath).string());

	// 2D(今は未使用)
	{
	}
	// 3D
	{
		cameraState_.transform3D.pos = Vector3::FromJson(data["transform3D.pos"]);
		cameraState_.transform3D.rotation = Vector3::FromJson(data["transform3D.rotation"]);
		cameraState_.perspectiveFovY = data["perspectiveFovY"].get<float>();
		cameraState_.perspectiveNearClip = data["perspectiveNearClip"].get<float>();
		cameraState_.perspectiveFarClip = data["perspectiveFarClip"].get<float>();
		cameraState_.perspectiveCullingMask = data["perspectiveCullingMask"].get<int32_t>();
	}
}

void Engine::SceneViewCameraController::Update(Dimension dimension) {

	// カメラの状態を更新できない場合は処理しない
	if (!CanUpdate()) {
		return;
	}

	switch (dimension) {
	case Engine::Dimension::Type2D:

		Update2D();
		break;
	case Engine::Dimension::Type3D:

		Update3D();
		break;
	}
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
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

void Engine::SceneViewCameraController::Update3D() {

	Input* input = Input::GetInstance();

	// マウスの移動量とホイールの移動量を取得
	const Vector2 mouseDelta = input->GetMouseMoveValue();
	const float wheel = input->GetMouseWheel();

	// 入力がないなら何もしない
	if (!input->PushMouseRight() && !input->PushMouseCenter() && wheel == 0.0f) {
		return;
	}

	// 操作感を合わせるために更新前の回転を使って行列を作る
	Vector3 prevEulerDeg = cameraState_.transform3D.rotation;

	Matrix4x4 rotateMatrix = Matrix4x4::MakeRotateMatrix(prevEulerDeg);
	Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(Vector3::AnyInit(1.0f), prevEulerDeg, cameraState_.transform3D.pos);

	const float rotateSpeedDeg = Math::RadToDeg(kRotateSpeed);

	// 右ドラッグ: 回転
	if (input->PushMouseRight()) {

		cameraState_.transform3D.rotation.x += mouseDelta.y * rotateSpeedDeg;
		cameraState_.transform3D.rotation.y += mouseDelta.x * rotateSpeedDeg;
		cameraState_.transform3D.rotation = Math::WrapDegree180(cameraState_.transform3D.rotation);
	}

	// 中ドラッグ: パン
	if (input->PushMouseCenter()) {

		Vector3 right = { kPanSpeed * mouseDelta.x, 0.0f, 0.0f };
		Vector3 up = { 0.0f, -kPanSpeed * mouseDelta.y, 0.0f };

		right = Vector3::TransferNormal(right, worldMatrix);
		up = Vector3::TransferNormal(up, worldMatrix);

		cameraState_.transform3D.pos += right + up;
	}

	// ホイール: 前後移動
	if (wheel != 0.0f) {

		Vector3 forward = { 0.0f, 0.0f, wheel * kZoomRate };
		forward = Vector3::TransferNormal(forward, rotateMatrix);

		cameraState_.transform3D.pos += forward;
	}
}

void Engine::SceneViewCameraController::Update2D() {}