#include "BaseCamera.h"

using namespace SakuEngine;

//============================================================================
//	include
//============================================================================
#include <Engine/Config.h>
#include <Engine/Core/Graphics/Renderer/Line/LineRenderer.h>
#include <Engine/Editor/Camera/CameraEditor.h>
#include <Engine/Utility/Enum/Direction.h>
#include <Engine/MathLib/MathUtils.h>

//============================================================================
//	BaseCamera classMethods
//============================================================================

BaseCamera::BaseCamera() {

	// 初期値設定
	updateDebugView_ = false;
	aspectRatio_ = Config::kWindowWidthf / Config::kWindowHeightf;
	frustumScale_ = 0.004f;

	fovY_ = 0.54f;
	nearClip_ = 0.1f;
	farClip_ = 3200.0f;

	// transformを一回初期化
	transform_.eulerRotate = Vector3(0.02f, 0.0f, 0.0f);
	transform_.scale = Vector3::AnyInit(1.0f);
	transform_.rotation = Quaternion::EulerToQuaternion(transform_.eulerRotate);
	transform_.translation = Vector3(0.0f, 1.8f, -24.0f);
	autoFucusTimer_.target_ = 0.32f;
	autoFucusTimer_.easingType_ = EasingType::EaseOutExpo;
}

void BaseCamera::StartAutoFocus(bool isFocus, const Vector3& target) {

	isStartFocus_ = isFocus;
	autoFucusTimer_.Reset();
	startFocusTranslation_ = transform_.translation;
	startFocusRotation_ = transform_.rotation;

	Vector3 direction = startFocusTranslation_ - target;
	direction = direction.Normalize();

	// 目標座標から一定距離離す
	const float targetOffset = 160.0f;
	targetFocusTranslation_ = target + direction * targetOffset;
	targetFocusRotation_ = Quaternion::LookRotation(Vector3(target - startFocusTranslation_).Normalize(),
		Direction::Get(Direction3D::Up));
}

void BaseCamera::StartCameraAnim(const std::string& animName, bool isAddFirstKey,
	bool isUpdateKey, const std::optional<KeyframeInverseSetting>& inverseSetting) {

	CameraEditor* editor = CameraEditor::GetInstance();

	// 名前が設定されていればアニメーションを再生
	if (!animName.empty()) {

		editor->StartAnim(animName, isAddFirstKey, isUpdateKey, inverseSetting);
	}
}

void BaseCamera::EndCameraAnim() {

	// アニメーションを終了させる
	CameraEditor::GetInstance()->EndAnim();
}

void BaseCamera::SetEditorParentTransform(const std::string& keyName, const Transform3D& parent) {

	CameraEditor::GetInstance()->SetParentTransform(keyName, parent);
}

void BaseCamera::BindEndEditCameraPose() {

	endEditTranslation_ = transform_.translation;
	endEditRotation_ = transform_.rotation;
	endEditFovY_ = fovY_;
}

void BaseCamera::UpdateView(UpdateMode updateMode) {

	// 自動フォーカス設定
	UpdateAutoFocus();

	// オイラーを設定して更新する
	if (updateMode == UpdateMode::Euler) {

		transform_.rotation = Quaternion::EulerToQuaternion(transform_.eulerRotate);
	}
	// 行列更新
	transform_.UpdateMatrix();

	viewMatrix_ = Matrix4x4::Inverse(transform_.matrix.world);
	projectionMatrix_ =
		Matrix4x4::MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
	viewProjectionMatrix_ = viewMatrix_ * projectionMatrix_;

	// billboardMatrixを計算
	CalBillboardMatrix();
}

void BaseCamera::UpdateAutoFocus() {

	if (!isStartFocus_) {
		return;
	}

	// 座標補間処理
	autoFucusTimer_.Update();
	transform_.translation = Vector3::Lerp(startFocusTranslation_,
		targetFocusTranslation_, autoFucusTimer_.easedT_);
	// 回転補間処理
	transform_.rotation = Quaternion::Slerp(startFocusRotation_,
		targetFocusRotation_, autoFucusTimer_.easedT_);
	transform_.eulerRotate = Quaternion::ToEulerAngles(Quaternion::Normalize(transform_.rotation));

	if (autoFucusTimer_.IsReached()) {

		// 補間終了
		transform_.translation = targetFocusTranslation_;
		transform_.rotation = targetFocusRotation_;
		transform_.eulerRotate = Quaternion::ToEulerAngles(Quaternion::Normalize(targetFocusRotation_));
		isStartFocus_ = false;
	}
}

void BaseCamera::ImGui() {

	ImGui::DragFloat3("translation##DebugCamera", &transform_.translation.x, 0.01f);
	if (ImGui::DragFloat3("rotation##DebugCamera", &transform_.eulerRotate.x, 0.01f)) {

		transform_.rotation = Quaternion::EulerToQuaternion(transform_.eulerRotate);
	}
	ImGui::Text("quaternion(%4.3f, %4.3f, %4.3f, %4.3f)",
		transform_.rotation.x, transform_.rotation.y, transform_.rotation.z, transform_.rotation.w);

	ImGui::DragFloat("fovY##DebugCamera", &fovY_, 0.01f);
	ImGui::DragFloat("farClip##DebugCamera", &farClip_, 1.0f);
}

void BaseCamera::EditFrustum() {

	ImGui::Checkbox("displayFrustum", &displayFrustum_);
	ImGui::DragFloat("frustumScale", &frustumScale_, 0.001f);
}

void BaseCamera::RenderFrustum() {

	if (!displayFrustum_) {
		return;
	}

	// カメラ空間でのコーナー計算
	float halfFovY = (fovY_ + 0.08f) * 0.5f;
	float heightNearHalf = std::tan(halfFovY) * nearClip_;
	float widthNearHalf = heightNearHalf * aspectRatio_;
	float heightFarHalf = std::tan(halfFovY) * farClip_;
	float widthFarHalf = heightFarHalf * aspectRatio_;

	Vector3 ncTL(-widthNearHalf, heightNearHalf, nearClip_);
	Vector3 ncTR(widthNearHalf, heightNearHalf, nearClip_);
	Vector3 ncBR(widthNearHalf, -heightNearHalf, nearClip_);
	Vector3 ncBL(-widthNearHalf, -heightNearHalf, nearClip_);

	Vector3 fcTL(-widthFarHalf, heightFarHalf, farClip_);
	Vector3 fcTR(widthFarHalf, heightFarHalf, farClip_);
	Vector3 fcBR(widthFarHalf, -heightFarHalf, farClip_);
	Vector3 fcBL(-widthFarHalf, -heightFarHalf, farClip_);

	ncTL *= frustumScale_;
	ncTR *= frustumScale_;
	ncBR *= frustumScale_;
	ncBL *= frustumScale_;

	fcTL *= frustumScale_;
	fcTR *= frustumScale_;
	fcBR *= frustumScale_;
	fcBL *= frustumScale_;

	Matrix4x4 cameraWorldMatrix = Matrix4x4::Inverse(viewMatrix_);

	// ワールド座標に変換
	Vector3 wncTL = Vector3::Transform(ncTL, cameraWorldMatrix);
	Vector3 wncTR = Vector3::Transform(ncTR, cameraWorldMatrix);
	Vector3 wncBR = Vector3::Transform(ncBR, cameraWorldMatrix);
	Vector3 wncBL = Vector3::Transform(ncBL, cameraWorldMatrix);

	Vector3 wfcTL = Vector3::Transform(fcTL, cameraWorldMatrix);
	Vector3 wfcTR = Vector3::Transform(fcTR, cameraWorldMatrix);
	Vector3 wfcBR = Vector3::Transform(fcBR, cameraWorldMatrix);
	Vector3 wfcBL = Vector3::Transform(fcBL, cameraWorldMatrix);

	Color color = Color::Yellow();
	LineRenderer* lineRenderer = LineRenderer::GetInstance();

	// 近クリップ
	lineRenderer->Get3D()->DrawLine(wncTL, wncTR, color);
	lineRenderer->Get3D()->DrawLine(wncTR, wncBR, color);
	lineRenderer->Get3D()->DrawLine(wncBR, wncBL, color);
	lineRenderer->Get3D()->DrawLine(wncBL, wncTL, color);
	// 遠クリップ
	lineRenderer->Get3D()->DrawLine(wfcTL, wfcTR, color);
	lineRenderer->Get3D()->DrawLine(wfcTR, wfcBR, color);
	lineRenderer->Get3D()->DrawLine(wfcBR, wfcBL, color);
	lineRenderer->Get3D()->DrawLine(wfcBL, wfcTL, color);
	// 近 → 遠
	lineRenderer->Get3D()->DrawLine(wncTL, wfcTL, color);
	lineRenderer->Get3D()->DrawLine(wncTR, wfcTR, color);
	lineRenderer->Get3D()->DrawLine(wncBR, wfcBR, color);
	lineRenderer->Get3D()->DrawLine(wncBL, wfcBL, color);
}

void BaseCamera::CalBillboardMatrix() {

	// billboardMatrixを計算する
	Matrix4x4 backToFrontMatrix = Matrix4x4::MakeYawMatrix(pi);

	billboardMatrix_ = Matrix4x4::Multiply(backToFrontMatrix, transform_.matrix.world);
	billboardMatrix_.m[3][0] = 0.0f;
	billboardMatrix_.m[3][1] = 0.0f;
	billboardMatrix_.m[3][2] = 0.0f;
}

void CameraLog::Output(const std::string& msg) {

	SpdLogger::LogGame(msg, SpdLogger::LogLevel::INFO);
}
