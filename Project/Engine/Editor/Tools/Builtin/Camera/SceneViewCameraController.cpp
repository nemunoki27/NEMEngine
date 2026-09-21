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
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>

//============================================================================
//	SceneViewCameraController classMethods
//============================================================================
namespace {

	// カメラ保存パス
	const std::string kCameraJsonPath = Engine::ConfigPaths::kSceneViewCamera;
	// 2Dカメラの既定値
	const Engine::Vector3 kDefault2DPosition = Engine::Vector3::AnyInit(0.0f);
	constexpr float kDefault2DZoom = 1.0f;
	constexpr float kMin2DZoom = 0.01f;
	constexpr float kMax2DZoom = 100.0f;
	constexpr float kDefault2DNearClip = 0.0f;
	constexpr float kDefault2DFarClip = 1000.0f;
	constexpr float kDefault2DPanSpeed = 1.0f;
	constexpr float kDefault2DZoomRate = 0.15f;
	// 3Dカメラの既定値
	const Engine::Vector3 kDefaultPosition = Engine::Vector3(-6.8f, 2.52f, -8.19f);
	const Engine::Vector3 kDefaultRotation = Engine::Vector3(13.75f, 37.8f, 0.0f);
	constexpr float kDefaultFovY = 30.9397202f;
	constexpr float kDefaultNearClip = 0.1f;
	constexpr float kDefaultFarClip = 8000.0f;
	constexpr float kDefaultRotateSpeed = 0.005f;
	constexpr float kDefaultZoomRate = 0.4f;
	constexpr float kDefaultPanSpeed = 0.02f;
	// フォーカス時に対象から離す距離
	constexpr float kFocusDistance = 20.0f;
	// フォーカスの寄り速度、1フレームあたりの補間率
	constexpr float kFocusLerpRate = 0.2f;
	// これ以下まで近づいたらフォーカス完了
	constexpr float kFocusReachEpsilon = 0.01f;

	// Vector3がカメラ計算に使える有限値か判定する
	bool IsFinite(const Engine::Vector3& value) {

		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	// JSONの数値を有限なfloatとして読み込む
	float ReadFiniteFloat(const nlohmann::json& data, const char* key, float defaultValue) {

		const auto it = data.find(key);
		if (it == data.end() || !it->is_number()) {
			return defaultValue;
		}

		const float value = static_cast<float>(it->get<double>());
		return std::isfinite(value) ? value : defaultValue;
	}

	// JSONの整数をint32_tの範囲内で読み込む
	int32_t ReadInt32(const nlohmann::json& data, const char* key, int32_t defaultValue) {

		const auto it = data.find(key);
		if (it == data.end() || !it->is_number_integer()) {
			return defaultValue;
		}

		const double value = it->get<double>();
		if (value < static_cast<double>((std::numeric_limits<int32_t>::min)()) ||
			value > static_cast<double>((std::numeric_limits<int32_t>::max)())) {
			return defaultValue;
		}
		return static_cast<int32_t>(value);
	}
}

Engine::SceneViewCameraController::SceneViewCameraController(
	bool persistSettings) : persistSettings_(persistSettings) {

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
	// 無効値は既定値へ戻し、次回起動用Configへnullを残さない
	if (!IsFinite(cameraState_.transform2D.pos)) {
		cameraState_.transform2D.pos = kDefault2DPosition;
	}
	cameraState_.orthographicZoom = std::clamp(
		std::isfinite(cameraState_.orthographicZoom) ? cameraState_.orthographicZoom : kDefault2DZoom,
		kMin2DZoom, kMax2DZoom);
	if (!std::isfinite(cameraState_.orthoNearClip)) {
		cameraState_.orthoNearClip = kDefault2DNearClip;
	}
	if (!std::isfinite(cameraState_.orthoFarClip) ||
		cameraState_.orthoFarClip <= cameraState_.orthoNearClip) {
		cameraState_.orthoFarClip = kDefault2DFarClip;
	}
	if (!IsFinite(cameraState_.transform3D.pos)) {
		cameraState_.transform3D.pos = kDefaultPosition;
	}
	if (!IsFinite(cameraState_.transform3D.rotation)) {
		cameraState_.transform3D.rotation = kDefaultRotation;
	}
	cameraState_.perspectiveFovY = std::clamp(
		std::isfinite(cameraState_.perspectiveFovY) ? cameraState_.perspectiveFovY : kDefaultFovY,
		1.0f, 179.0f);
	if (!std::isfinite(cameraState_.perspectiveNearClip) || cameraState_.perspectiveNearClip <= 0.0f) {
		cameraState_.perspectiveNearClip = kDefaultNearClip;
	}
	if (!std::isfinite(cameraState_.perspectiveFarClip) ||
		cameraState_.perspectiveFarClip <= cameraState_.perspectiveNearClip) {
		cameraState_.perspectiveFarClip = kDefaultFarClip;
	}
	rotateSpeed_ = std::isfinite(rotateSpeed_) && rotateSpeed_ >= 0.0f ?
		rotateSpeed_ : kDefaultRotateSpeed;
	zoomRate_ = std::isfinite(zoomRate_) && zoomRate_ >= 0.0f ? zoomRate_ : kDefaultZoomRate;
	panSpeed_ = std::isfinite(panSpeed_) && panSpeed_ >= 0.0f ? panSpeed_ : kDefaultPanSpeed;
	zoomRate2D_ = std::isfinite(zoomRate2D_) && zoomRate2D_ >= 0.0f ?
		zoomRate2D_ : kDefault2DZoomRate;
	panSpeed2D_ = std::isfinite(panSpeed2D_) && panSpeed2D_ >= 0.0f ?
		panSpeed2D_ : kDefault2DPanSpeed;

	// カメラを閉じた瞬間の状態を保存する
	nlohmann::json data{};

	// 2D
	{
		JsonAdapter::SetVector3(data, "transform2D.pos", cameraState_.transform2D.pos);
		data["orthographicZoom"] = cameraState_.orthographicZoom;
		data["orthoNearClip"] = cameraState_.orthoNearClip;
		data["orthoFarClip"] = cameraState_.orthoFarClip;
		data["orthographicCullingMask"] = cameraState_.orthographicCullingMask;
		data["zoomRate2D"] = zoomRate2D_;
		data["panSpeed2D"] = panSpeed2D_;
	}
	// 3D
	{
		JsonAdapter::SetVector3(data, "transform3D.pos", cameraState_.transform3D.pos);
		JsonAdapter::SetVector3(data, "transform3D.rotation", cameraState_.transform3D.rotation);
		data["perspectiveFovY"] = cameraState_.perspectiveFovY;
		data["perspectiveNearClip"] = cameraState_.perspectiveNearClip;
		data["perspectiveFarClip"] = cameraState_.perspectiveFarClip;
		data["perspectiveCullingMask"] = cameraState_.perspectiveCullingMask;
	}
	// カメラ操作速度
	{
		data["rotateSpeed"] = rotateSpeed_;
		data["zoomRate"] = zoomRate_;
		data["panSpeed"] = panSpeed_;
	}

	JsonAdapter::SaveCanonical(savePath_, data);
}

void Engine::SceneViewCameraController::MakeDefaultState() {

	cameraState_ = {};

	cameraState_.transform2D.pos = kDefault2DPosition;
	cameraState_.transform2D.rotation = Vector3::AnyInit(0.0f);

	cameraState_.transform3D.pos = kDefaultPosition;
	cameraState_.transform3D.rotation = kDefaultRotation;

	cameraState_.enableOrthographic = true;
	cameraState_.orthoNearClip = kDefault2DNearClip;
	cameraState_.orthoFarClip = kDefault2DFarClip;
	cameraState_.orthographicZoom = kDefault2DZoom;
	cameraState_.orthographicCullingMask = -1;

	cameraState_.enablePerspective = true;
	cameraState_.perspectiveFovY = kDefaultFovY;
	cameraState_.perspectiveNearClip = kDefaultNearClip;
	cameraState_.perspectiveFarClip = kDefaultFarClip;
	cameraState_.perspectiveCullingMask = -1;
}

void Engine::SceneViewCameraController::MakeFromJson(const std::string& filePath) {

	// 保存したカメラデータがあれば読みこんで設定する
	const nlohmann::json data = JsonAdapter::Load(filePath, false);
	if (!data.is_object()) {
		return;
	}

	// 2D
	{
		cameraState_.transform2D.pos = JsonAdapter::GetVector3(
			data, "transform2D.pos", cameraState_.transform2D.pos);
		cameraState_.orthographicZoom = std::clamp(
			ReadFiniteFloat(data, "orthographicZoom", cameraState_.orthographicZoom),
			kMin2DZoom, kMax2DZoom);
		cameraState_.orthoNearClip = ReadFiniteFloat(
			data, "orthoNearClip", cameraState_.orthoNearClip);
		cameraState_.orthoFarClip = ReadFiniteFloat(
			data, "orthoFarClip", cameraState_.orthoFarClip);
		cameraState_.orthographicCullingMask = ReadInt32(
			data, "orthographicCullingMask", cameraState_.orthographicCullingMask);
		if (cameraState_.orthoFarClip <= cameraState_.orthoNearClip) {
			cameraState_.orthoFarClip = kDefault2DFarClip;
		}
	}
	// 3D
	{
		cameraState_.transform3D.pos = JsonAdapter::GetVector3(
			data, "transform3D.pos", cameraState_.transform3D.pos);
		cameraState_.transform3D.rotation = JsonAdapter::GetVector3(
			data, "transform3D.rotation", cameraState_.transform3D.rotation);
		cameraState_.perspectiveFovY = std::clamp(
			ReadFiniteFloat(data, "perspectiveFovY", cameraState_.perspectiveFovY), 1.0f, 179.0f);
		cameraState_.perspectiveNearClip = ReadFiniteFloat(
			data, "perspectiveNearClip", cameraState_.perspectiveNearClip);
		cameraState_.perspectiveFarClip = ReadFiniteFloat(
			data, "perspectiveFarClip", cameraState_.perspectiveFarClip);
		cameraState_.perspectiveCullingMask = ReadInt32(
			data, "perspectiveCullingMask", cameraState_.perspectiveCullingMask);
		if (cameraState_.perspectiveNearClip <= 0.0f) {
			cameraState_.perspectiveNearClip = kDefaultNearClip;
		}
		if (cameraState_.perspectiveFarClip <= cameraState_.perspectiveNearClip) {
			cameraState_.perspectiveFarClip = kDefaultFarClip;
		}
	}
	// カメラ操作速度、古いConfigにキーが無ければ既定値を保つ
	{
		zoomRate2D_ = std::max(0.0f, ReadFiniteFloat(data, "zoomRate2D", zoomRate2D_));
		panSpeed2D_ = std::max(0.0f, ReadFiniteFloat(data, "panSpeed2D", panSpeed2D_));
		rotateSpeed_ = std::max(0.0f, ReadFiniteFloat(data, "rotateSpeed", rotateSpeed_));
		zoomRate_ = std::max(0.0f, ReadFiniteFloat(data, "zoomRate", zoomRate_));
		panSpeed_ = std::max(0.0f, ReadFiniteFloat(data, "panSpeed", panSpeed_));
	}
}

void Engine::SceneViewCameraController::Update(Dimension dimension, InputViewArea viewArea) {

	// 選択次元に関係なく2Dと3Dを同時描画し、入力対象のカメラだけを切り替える
	cameraState_.enableOrthographic = true;
	cameraState_.enablePerspective = true;

	// 外部編集を含めて無効なTransformやズーム値を描画へ渡さない
	if (!IsFinite(cameraState_.transform2D.pos)) {
		cameraState_.transform2D.pos = kDefault2DPosition;
	}
	cameraState_.orthographicZoom = std::clamp(
		std::isfinite(cameraState_.orthographicZoom) ? cameraState_.orthographicZoom : kDefault2DZoom,
		kMin2DZoom, kMax2DZoom);
	if (!IsFinite(cameraState_.transform3D.pos) || !IsFinite(cameraState_.transform3D.rotation)) {
		cameraState_.transform3D.pos = kDefaultPosition;
		cameraState_.transform3D.rotation = kDefaultRotation;
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

	// 現在の前方を保ったまま対象が画面中心へ来る位置を寄り先にする、回転は変えないので注視になる
	const Matrix4x4 rotateMatrix = Matrix4x4::MakeRotateMatrix(cameraState_.transform3D.rotation);
	const Vector3 forward = Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f), rotateMatrix);
	focusTargetPos_ = worldPosition - forward * kFocusDistance;
	focusActive_ = true;
}

void Engine::SceneViewCameraController::UpdateFocus() {

	if (!focusActive_) {
		return;
	}
	if (!IsFinite(cameraState_.transform3D.pos) || !IsFinite(focusTargetPos_)) {
		cameraState_.transform3D.pos = kDefaultPosition;
		focusActive_ = false;
		return;
	}

	// 寄り先へ滑らかに近づける、到達したら完了する
	cameraState_.transform3D.pos = Vector3::Lerp(cameraState_.transform3D.pos, focusTargetPos_, kFocusLerpRate);
	if ((focusTargetPos_ - cameraState_.transform3D.pos).Length() <= kFocusReachEpsilon) {

		cameraState_.transform3D.pos = focusTargetPos_;
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
	const Vector3 previousPosition = cameraState_.transform3D.pos;
	const Vector3 previousRotation = cameraState_.transform3D.rotation;

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
	Vector3 prevEulerDeg = cameraState_.transform3D.rotation;

	Matrix4x4 rotateMatrix = Matrix4x4::MakeRotateMatrix(prevEulerDeg);
	Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(Vector3::AnyInit(1.0f), prevEulerDeg, cameraState_.transform3D.pos);

	const float rotateSpeedDeg = Math::RadToDeg(rotateSpeed_);

	// 右ドラッグ:回転
	if (input->PushMouseRight()) {

		cameraState_.transform3D.rotation.x += mouseDelta.y * rotateSpeedDeg;
		cameraState_.transform3D.rotation.y += mouseDelta.x * rotateSpeedDeg;
		cameraState_.transform3D.rotation = Math::WrapDegree180(cameraState_.transform3D.rotation);
	}

	// 中ドラッグ:パン
	if (input->PushMouseCenter()) {

		Vector3 right = { panSpeed_ * mouseDelta.x, 0.0f, 0.0f };
		Vector3 up = { 0.0f, -panSpeed_ * mouseDelta.y, 0.0f };

		right = Vector3::TransferNormal(right, worldMatrix);
		up = Vector3::TransferNormal(up, worldMatrix);

		cameraState_.transform3D.pos += right + up;
	}

	// ホイール:前後移動
	if (wheel != 0.0f) {

		Vector3 forward = { 0.0f, 0.0f, wheel * zoomRate_ };
		forward = Vector3::TransferNormal(forward, rotateMatrix);

		cameraState_.transform3D.pos += forward;
	}

	// 入力値や行列が壊れた場合は最後の正常値を保つ
	if (!IsFinite(cameraState_.transform3D.pos) || !IsFinite(cameraState_.transform3D.rotation)) {
		cameraState_.transform3D.pos = IsFinite(previousPosition) ? previousPosition : kDefaultPosition;
		cameraState_.transform3D.rotation = IsFinite(previousRotation) ? previousRotation : kDefaultRotation;
	}
}

void Engine::SceneViewCameraController::Update2D(InputViewArea viewArea) {

	Input* input = Input::GetInstance();
	const Vector3 previousPosition = cameraState_.transform2D.pos;
	const float previousZoom = cameraState_.orthographicZoom;
	const Vector2 mouseDelta = input->GetMouseMoveValueInView(viewArea);
	const float wheel = input->GetMouseWheel();

	// 中ドラッグ:上下左右移動
	if (input->PushMouseCenter()) {
		const float worldUnitsPerPixel = 1.0f / cameraState_.orthographicZoom;
		cameraState_.transform2D.pos.x -= mouseDelta.x * panSpeed2D_ * worldUnitsPerPixel;
		cameraState_.transform2D.pos.y -= mouseDelta.y * panSpeed2D_ * worldUnitsPerPixel;
	}

	// ホイール:カーソル位置を維持したズーム
	if (wheel != 0.0f) {
		const float nextZoom = std::clamp(
			previousZoom * std::exp(wheel * zoomRate2D_), kMin2DZoom, kMax2DZoom);
		if (const std::optional<Vector2> mouse = input->GetMousePosInView(viewArea)) {
			cameraState_.transform2D.pos.x += mouse->x / previousZoom - mouse->x / nextZoom;
			cameraState_.transform2D.pos.y += mouse->y / previousZoom - mouse->y / nextZoom;
		}
		cameraState_.orthographicZoom = nextZoom;
	}

	// 入力値が壊れた場合は最後の正常値へ戻す
	if (!IsFinite(cameraState_.transform2D.pos) ||
		!std::isfinite(cameraState_.orthographicZoom)) {
		cameraState_.transform2D.pos = previousPosition;
		cameraState_.orthographicZoom = previousZoom;
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
				cameraState_.transform2D.pos = kDefault2DPosition;
				cameraState_.orthographicZoom = kDefault2DZoom;
				cameraState_.orthoNearClip = kDefault2DNearClip;
				cameraState_.orthoFarClip = kDefault2DFarClip;
				cameraState_.orthographicCullingMask = -1;
				zoomRate2D_ = kDefault2DZoomRate;
				panSpeed2D_ = kDefault2DPanSpeed;
			}
			ImGui::DragFloat2("位置", &cameraState_.transform2D.pos.x, 1.0f);
			ImGui::DragFloat("ズーム", &cameraState_.orthographicZoom,
				0.01f, kMin2DZoom, kMax2DZoom, "%.3f");
			ImGui::DragFloat("ニアクリップ", &cameraState_.orthoNearClip, 0.1f);
			ImGui::DragFloat("ファークリップ", &cameraState_.orthoFarClip, 1.0f);
			ImGui::DragInt("カリングマスク", &cameraState_.orthographicCullingMask);
			ImGui::DragFloat("移動速度", &panSpeed2D_, 0.01f, 0.0f, 100.0f, "%.3f");
			ImGui::DragFloat("ズーム速度", &zoomRate2D_, 0.01f, 0.0f, 10.0f, "%.3f");

			ImGui::PopID();
		}
		ImGui::SeparatorText("3D");
		{
			ImGui::PushID("SceneViewCamera3D");

			// パラメータリセット
			if (ImGui::Button("Reset Camera")) {
				cameraState_.perspectiveFovY = Math::RadToDeg(0.54f);
				cameraState_.perspectiveNearClip = 0.1f;
				cameraState_.perspectiveFarClip = 4000.0f;
				cameraState_.perspectiveCullingMask = -1;
			}

			ImGui::DragFloat("perspectiveFovY", &cameraState_.perspectiveFovY, 0.1f, 1.0f, 179.0f);
			ImGui::DragFloat("perspectiveNearClip", &cameraState_.perspectiveNearClip, 0.01f);
			ImGui::DragFloat("perspectiveFarClip", &cameraState_.perspectiveFarClip, 1.0f);
			ImGui::DragInt("perspectiveCullingMask", &cameraState_.perspectiveCullingMask);

			ImGui::PopID();
		}
		ImGui::SeparatorText("操作速度");
		{
			ImGui::PushID("SceneViewCameraControl");

			ImGui::DragFloat("回転速度", &rotateSpeed_, 0.0001f, 0.0f, 1.0f, "%.4f");
			ImGui::DragFloat("ズーム速度", &zoomRate_, 0.01f, 0.0f, 100.0f);
			ImGui::DragFloat("パン速度", &panSpeed_, 0.001f, 0.0f, 10.0f, "%.3f");

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
