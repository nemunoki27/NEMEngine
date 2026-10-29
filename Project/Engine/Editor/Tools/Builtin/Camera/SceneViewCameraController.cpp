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

//============================================================================
//	SceneViewCameraController classMethods
//============================================================================
using namespace Engine::SceneViewCameraDefaults;
using Engine::SceneViewCameraSettingsUtility::IsFinite;

namespace {

	// カメラ保存パス
	const std::string kCameraJsonPath = Engine::ConfigPaths::kSceneViewCamera;
	// フォーカス時に対象から離す距離
	constexpr float kFocusDistance = kDefaultOrbitDistance;
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
	navigationState_ = {};
	CancelNavigation();
}

void Engine::SceneViewCameraController::MakeFromJson(const std::string& filePath) {

	// ファイルがなければ現在の設定を保つ
	SceneViewCameraSettingsUtility::Load(filePath, settings_);
	navigationState_ = {};
	CancelNavigation();
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
		navigationState_ = {};
	}

	// カメラの状態を更新できない場合は処理しない
	if (!CanUpdate(viewArea)) {
		CancelNavigation();
		return;
	}

	switch (dimension) {
	case Engine::Dimension::Type2D:

		Update2D(viewArea);
		break;
	case Engine::Dimension::Type3D:

		Update3D(viewArea);
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
	navigationState_.pivot = settings_.cameraState.transform3D.pos + forward * kFocusDistance;
	navigationState_.pivotValid = true;
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
	const Vector3 forward = Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f),
		Matrix4x4::MakeRotateMatrix(settings_.cameraState.transform3D.rotation));
	navigationState_.pivot = settings_.cameraState.transform3D.pos + forward * kFocusDistance;
	navigationState_.pivotValid = true;
	if ((focusTargetPos_ - settings_.cameraState.transform3D.pos).Length() <= kFocusReachEpsilon) {

		settings_.cameraState.transform3D.pos = focusTargetPos_;
		navigationState_.pivot = focusTargetPos_ + forward * kFocusDistance;
		focusActive_ = false;
	}
}

bool Engine::SceneViewCameraController::CanUpdate(InputViewArea viewArea) {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	Input* input = Input::GetInstance();
	if (!input || !input->HasWindowFocus()) {
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

void Engine::SceneViewCameraController::OpenEditorTool() {

	openWindow_ = true;
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
