#include "SceneViewCameraController.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <cmath>
#include <optional>

//============================================================================
//	SceneViewCameraController classMethods
//============================================================================
using namespace Engine::SceneViewCameraDefaults;
using Engine::SceneViewCameraSettingsUtility::IsFinite;

namespace {

	// カメラ保存パス
	const std::string kCameraJsonPath = Engine::ConfigPaths::kSceneViewCamera;
	// フォーカス時に対象から離す距離
	constexpr float kFocusDistance = 20.0f;
	// フォーカスの寄り速度、1フレームあたりの補間率
	constexpr float kFocusLerpRate = 0.2f;
	// これ以下まで近づいたらフォーカス完了
	constexpr float kFocusReachEpsilon = 0.01f;

}

Engine::SceneViewCameraController::SceneViewCameraController(bool persistSettings) : persistSettings_(persistSettings) {

	MakeDefaultState();
	if (!persistSettings_) {
		return;
	}
	// jsonから元のカメラ復元
	MakeFromJson(RuntimePaths::GetUserSettingsPath(kCameraJsonPath).string());
	savePath_ = RuntimePaths::GetUserSettingsPath(kCameraJsonPath).string();
}

Engine::SceneViewCameraController::~SceneViewCameraController() {

	if (!persistSettings_ || savePath_.empty()) {
		return;
	}
	// 終了時の設定を保存し、失敗を診断へ残す
	if (!SceneViewCameraSettingsUtility::Save(savePath_, settings_)) {
		Logger::Output(LogType::Engine, spdlog::level::err, "シーンカメラ設定を保存できません path={}", savePath_);
	}
}

void Engine::SceneViewCameraController::MakeDefaultState() {

	// 操作速度を保ってカメラだけを初期化する
	settings_.cameraState = SceneViewCameraSettings{}.cameraState;
}

void Engine::SceneViewCameraController::MakeFromJson(const std::string& filePath) {

	// ファイルがなければ現在の設定を保つ
	SceneViewCameraSettingsUtility::Load(filePath, settings_);
}

void Engine::SceneViewCameraController::Update(Dimension dimension, InputViewArea viewArea) {

	// 2Dと3Dの描画を有効にする
	settings_.cameraState.enableOrthographic = true;
	settings_.cameraState.enablePerspective = true;

	// 無効な位置とズームを既定値へ戻す
	if (!IsFinite(settings_.cameraState.transform2D.pos)) {
		settings_.cameraState.transform2D.pos = kDefault2DPosition;
	}
	settings_.cameraState.orthographicZoom = std::clamp(
		std::isfinite(settings_.cameraState.orthographicZoom) ? settings_.cameraState.orthographicZoom : kDefault2DZoom,
		kMin2DZoom, kMax2DZoom);
	if (!IsFinite(settings_.cameraState.transform3D.pos) || !IsFinite(settings_.cameraState.transform3D.rotation)) {
		settings_.cameraState.transform3D.pos = kDefaultPosition;
		settings_.cameraState.transform3D.rotation = kDefaultRotation;
		focusActive_ = false;
	}

	// カメラの状態を更新できない場合は処理しない
	if (!CanUpdate(viewArea)) {
		return;
	}

	switch (dimension) {
	case Engine::Dimension::Type2D:

		Update2D(viewArea);
		break;
	case Engine::Dimension::Type3D:

		Update3D();
		break;
	}
}

void Engine::SceneViewCameraController::FocusOn(const Vector3& worldPosition) {

	if (!IsFinite(worldPosition)) {
		return;
	}

	// 回転を保って対象の手前へ寄る
	const Matrix4x4 rotateMatrix = Matrix4x4::MakeRotateMatrix(settings_.cameraState.transform3D.rotation);
	const Vector3 forward = Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f), rotateMatrix);
	focusTargetPos_ = worldPosition - forward * kFocusDistance;
	focusActive_ = true;
}

void Engine::SceneViewCameraController::UpdateFocus() {

	if (!focusActive_) {
		return;
	}
	if (!IsFinite(settings_.cameraState.transform3D.pos) || !IsFinite(focusTargetPos_)) {
		settings_.cameraState.transform3D.pos = kDefaultPosition;
		focusActive_ = false;
		return;
	}

	// 寄り先へ近づけ、到達時に補間を終了する
	settings_.cameraState.transform3D.pos =
		Vector3::Lerp(settings_.cameraState.transform3D.pos, focusTargetPos_, kFocusLerpRate);
	if ((focusTargetPos_ - settings_.cameraState.transform3D.pos).Length() <= kFocusReachEpsilon) {

		settings_.cameraState.transform3D.pos = focusTargetPos_;
		focusActive_ = false;
	}
}

bool Engine::SceneViewCameraController::CanUpdate(InputViewArea viewArea) {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	Input* input = Input::GetInstance();
	if (!input) {
		return false;
	}
	// シーンビュー内でマウスが操作されているか
	if (!input->HasViewRect(viewArea)) {
		return false;
	}
	if (!input->IsMouseOnView(viewArea)) {
		return false;
	}
	return true;
#else
	return false;
#endif
}

void Engine::SceneViewCameraController::Update3D() {

	Input* input = Input::GetInstance();
	const Vector3 previousPosition = settings_.cameraState.transform3D.pos;
	const Vector3 previousRotation = settings_.cameraState.transform3D.rotation;

	// マウスの移動量とホイールの移動量を取得
	const Vector2 mouseDelta = input->GetMouseMoveValue();
	const float wheel = input->GetMouseWheel();

	// 入力がないなら何もしない
	if (!input->PushMouseRight() && !input->PushMouseCenter() && wheel == 0.0f) {
		return;
	}

	// 手動操作が入ったらフォーカスを中断する
	focusActive_ = false;

	// 操作感を合わせるために更新前の回転を使って行列を作る
	Vector3 prevEulerDeg = settings_.cameraState.transform3D.rotation;

	Matrix4x4 rotateMatrix = Matrix4x4::MakeRotateMatrix(prevEulerDeg);
	Matrix4x4 worldMatrix =
		Matrix4x4::MakeAffineMatrix(Vector3::AnyInit(1.0f), prevEulerDeg, settings_.cameraState.transform3D.pos);

	const float rotateSpeedDeg = Math::RadToDeg(settings_.rotateSpeed);

	// 右ドラッグ:回転
	if (input->PushMouseRight()) {

		settings_.cameraState.transform3D.rotation.x += mouseDelta.y * rotateSpeedDeg;
		settings_.cameraState.transform3D.rotation.y += mouseDelta.x * rotateSpeedDeg;
		settings_.cameraState.transform3D.rotation = Math::WrapDegree180(settings_.cameraState.transform3D.rotation);
	}

	// 中ドラッグ:パン
	if (input->PushMouseCenter()) {

		Vector3 right = {settings_.panSpeed * mouseDelta.x, 0.0f, 0.0f};
		Vector3 up = {0.0f, -settings_.panSpeed * mouseDelta.y, 0.0f};

		right = Vector3::TransferNormal(right, worldMatrix);
		up = Vector3::TransferNormal(up, worldMatrix);

		settings_.cameraState.transform3D.pos += right + up;
	}

	// ホイール:前後移動
	if (wheel != 0.0f) {

		Vector3 forward = {0.0f, 0.0f, wheel * settings_.zoomRate};
		forward = Vector3::TransferNormal(forward, rotateMatrix);

		settings_.cameraState.transform3D.pos += forward;
	}

	// 入力値や行列が壊れた場合は最後の正常値を保つ
	if (!IsFinite(settings_.cameraState.transform3D.pos) || !IsFinite(settings_.cameraState.transform3D.rotation)) {
		settings_.cameraState.transform3D.pos = IsFinite(previousPosition) ? previousPosition : kDefaultPosition;
		settings_.cameraState.transform3D.rotation = IsFinite(previousRotation) ? previousRotation : kDefaultRotation;
	}
}

void Engine::SceneViewCameraController::Update2D(InputViewArea viewArea) {

	Input* input = Input::GetInstance();
	const Vector3 previousPosition = settings_.cameraState.transform2D.pos;
	const float previousZoom = settings_.cameraState.orthographicZoom;
	const Vector2 mouseDelta = input->GetMouseMoveValueInView(viewArea);
	const float wheel = input->GetMouseWheel();

	// 中ドラッグ:上下左右移動
	if (input->PushMouseCenter()) {
		const float worldUnitsPerPixel = 1.0f / settings_.cameraState.orthographicZoom;
		settings_.cameraState.transform2D.pos.x -= mouseDelta.x * settings_.panSpeed2D * worldUnitsPerPixel;
		settings_.cameraState.transform2D.pos.y -= mouseDelta.y * settings_.panSpeed2D * worldUnitsPerPixel;
	}

	// ホイール:カーソル位置を維持したズーム
	if (wheel != 0.0f) {
		const float nextZoom = std::clamp(previousZoom * std::exp(wheel * settings_.zoomRate2D), kMin2DZoom, kMax2DZoom);
		if (const std::optional<Vector2> mouse = input->GetMousePosInView(viewArea)) {
			settings_.cameraState.transform2D.pos.x += mouse->x / previousZoom - mouse->x / nextZoom;
			settings_.cameraState.transform2D.pos.y += mouse->y / previousZoom - mouse->y / nextZoom;
		}
		settings_.cameraState.orthographicZoom = nextZoom;
	}

	// 入力値が壊れた場合は最後の正常値へ戻す
	if (!IsFinite(settings_.cameraState.transform2D.pos) || !std::isfinite(settings_.cameraState.orthographicZoom)) {
		settings_.cameraState.transform2D.pos = previousPosition;
		settings_.cameraState.orthographicZoom = previousZoom;
	}
}

void Engine::SceneViewCameraController::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::SceneViewCameraController::DrawEditorTool([[maybe_unused]] const EditorToolContext& context) {

	if (openWindow_) {

		if (!ImGui::Begin("SceneViewCamera", &openWindow_)) {
			ImGui::End();
			return;
		}

		ImGui::SeparatorText("2D");
		{
			ImGui::PushID("SceneViewCamera2D");

			if (ImGui::Button("リセット")) {
				settings_.cameraState.transform2D.pos = kDefault2DPosition;
				settings_.cameraState.orthographicZoom = kDefault2DZoom;
				settings_.cameraState.orthoNearClip = kDefault2DNearClip;
				settings_.cameraState.orthoFarClip = kDefault2DFarClip;
				settings_.cameraState.orthographicCullingMask = -1;
				settings_.zoomRate2D = kDefault2DZoomRate;
				settings_.panSpeed2D = kDefault2DPanSpeed;
			}
			ImGui::DragFloat2("位置", &settings_.cameraState.transform2D.pos.x, 1.0f);
			ImGui::DragFloat("ズーム", &settings_.cameraState.orthographicZoom, 0.01f, kMin2DZoom, kMax2DZoom, "%.3f");
			ImGui::DragFloat("ニアクリップ", &settings_.cameraState.orthoNearClip, 0.1f);
			ImGui::DragFloat("ファークリップ", &settings_.cameraState.orthoFarClip, 1.0f);
			ImGui::DragInt("カリングマスク", &settings_.cameraState.orthographicCullingMask);
			ImGui::DragFloat("移動速度", &settings_.panSpeed2D, 0.01f, 0.0f, 100.0f, "%.3f");
			ImGui::DragFloat("ズーム速度", &settings_.zoomRate2D, 0.01f, 0.0f, 10.0f, "%.3f");

			ImGui::PopID();
		}
		ImGui::SeparatorText("3D");
		{
			ImGui::PushID("SceneViewCamera3D");

			// パラメータリセット
			if (ImGui::Button("Reset Camera")) {
				settings_.cameraState.perspectiveFovY = Math::RadToDeg(0.54f);
				settings_.cameraState.perspectiveNearClip = 0.1f;
				settings_.cameraState.perspectiveFarClip = 4000.0f;
				settings_.cameraState.perspectiveCullingMask = -1;
			}

			ImGui::DragFloat("perspectiveFovY", &settings_.cameraState.perspectiveFovY, 0.1f, 1.0f, 179.0f);
			ImGui::DragFloat("perspectiveNearClip", &settings_.cameraState.perspectiveNearClip, 0.01f);
			ImGui::DragFloat("perspectiveFarClip", &settings_.cameraState.perspectiveFarClip, 1.0f);
			ImGui::DragInt("perspectiveCullingMask", &settings_.cameraState.perspectiveCullingMask);

			ImGui::PopID();
		}
		ImGui::SeparatorText("操作速度");
		{
			ImGui::PushID("SceneViewCameraControl");

			ImGui::DragFloat("回転速度", &settings_.rotateSpeed, 0.0001f, 0.0f, 1.0f, "%.4f");
			ImGui::DragFloat("ズーム速度", &settings_.zoomRate, 0.01f, 0.0f, 100.0f);
			ImGui::DragFloat("パン速度", &settings_.panSpeed, 0.001f, 0.0f, 10.0f, "%.3f");

			ImGui::PopID();
		}

		ImGui::End();
	}
}

//============================================================================
//	SceneViewCameraController classMethods
//============================================================================

namespace Engine {

	void SceneViewCameraController::SetSavePath(const std::string& savePath) {

		savePath_ = savePath;
		persistSettings_ = !savePath_.empty();
	}
}
