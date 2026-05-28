#include "ConicalPendulum.h"

using namespace SakuEngine;

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Timer/GameTimer.h>
#include <Engine/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Graphics/Renderer/Line/LineRenderer.h>

// imgui
#include <imgui.h>

//============================================================================
//	ConicalPendulum classMethods
//============================================================================

void ConicalPendulum::Init() {

	// 初期化値
	anchor = Vector3(0.0f, 16.0f, 0.0f);
	length = 8.0f;
	angularVelocity = 1.0f;
	halfApexAngle = pi / 6.0f;
	angle = 0.0f;
	moveSpeed = 1.0f;
	minAngle = 0.0f;
	maxAngle = pi;
	rotation = Quaternion::Identity();
	reachCount = 0;
}

void ConicalPendulum::Reset(bool isStartMin) {

	reachCount = 0;

	if (isStartMin) {

		// minAngleからmaxAngle
		angle = minAngle;
		angularVelocity = 1.0f;
	} else {

		// maxAngleからminAngle
		angle = maxAngle;
		angularVelocity = -1.0f;
	}
	// 初期位置を計算
	float radius = std::sin(halfApexAngle) * length;
	float height = std::cos(halfApexAngle) * length;

	Vector3 local(radius * std::cos(angle), -height, radius * std::sin(angle));
	Vector3 rotated = rotation * local;
	currentPos = anchor + rotated;
}

Vector3 ConicalPendulum::GetMinPos() const {

	// 半径と高さ
	float radius = std::sin(halfApexAngle) * length;
	float height = std::cos(halfApexAngle) * length;

	// ローカル位置
	Vector3 local = Vector3(radius * std::cos(minAngle), -height, radius * std::sin(minAngle));
	// 回転させた位置
	Vector3 rotated = rotation * local;

	// 位置を設定
	Vector3 result = anchor + rotated;

	return result;
}

Vector3 ConicalPendulum::GetMaxPos() const {

	// 半径と高さ
	float radius = std::sin(halfApexAngle) * length;
	float height = std::cos(halfApexAngle) * length;

	// ローカル位置
	Vector3 local = Vector3(radius * std::cos(maxAngle), -height, radius * std::sin(maxAngle));
	// 回転させた位置
	Vector3 rotated = rotation * local;

	// 位置を設定
	Vector3 result = anchor + rotated;

	return result;
}

void ConicalPendulum::Update() {

	// 角速度を計算
	float angularSpeed = std::sqrt(9.8f / (length * std::cos(halfApexAngle))) * moveSpeed;
	if (0.0f <= angularVelocity) {

		angularVelocity = angularSpeed;
	} else {

		angularVelocity = -angularSpeed;
	}
	// 角度を更新
	angle += angularVelocity * SakuEngine::GameTimer::GetScaledDeltaTime();
	if (angle > maxAngle) {

		// ここで反転
		angle = maxAngle;
		angularVelocity = -angularSpeed;
		++reachCount;
	} else if (angle < minAngle) {

		// ここで反転
		angle = minAngle;
		angularVelocity = angularSpeed;
		++reachCount;
	}

	easedAngle = angle;
	float angleRange = maxAngle - minAngle;
	if (angleRange != 0.0f) {

		// 片道分の0〜1の進行度
		float t = 0.0f;
		if (angularVelocity >= 0.0f) {

			// min->maxへ向かっているとき
			// 0〜1
			t = (angle - minAngle) / angleRange;
		} else {
			// max->minへ向かっているとき
			// 0〜1
			t = (maxAngle - angle) / angleRange;
		}
		t = std::clamp(t, 0.0f, 1.0f);
		float easedT = EasedValue(easingType, t);

		// イージングしたtを角度に戻す
		if (angularVelocity >= 0.0f) {

			easedAngle = minAngle + easedT * angleRange;
		} else {

			easedAngle = maxAngle - easedT * angleRange;
		}
	}


	// 半径と高さ
	float radius = std::sin(halfApexAngle) * length;
	float height = std::cos(halfApexAngle) * length;

	// ローカル位置
	Vector3 local = Vector3(radius * std::cos(easedAngle), -height, radius * std::sin(easedAngle));
	// 回転させた位置
	Vector3 rotated = rotation * local;

	// 位置を設定
	currentPos = anchor + rotated;
}

void ConicalPendulum::DrawDebugLine() {

	LineRenderer* lineRenderer = SakuEngine::LineRenderer::GetInstance();

	// 支点から現在位置への線を描画
	lineRenderer->Get3D()->DrawLine(anchor, currentPos, SakuEngine::Color::Cyan());

	// アンカー位置、現在位置を球で描画
	lineRenderer->Get3D()->DrawSphere(6, 0.4f, anchor, SakuEngine::Color::Cyan());
	lineRenderer->Get3D()->DrawSphere(6, 0.4f, currentPos, SakuEngine::Color::Cyan());
	// 最小角度位置と最大角度位置を球で描画
	lineRenderer->Get3D()->DrawSphere(6, 0.4f, GetMinPos(), SakuEngine::Color::Cyan());
	lineRenderer->Get3D()->DrawSphere(6, 0.4f, GetMaxPos(), SakuEngine::Color::Cyan());

	// 円錐振り子の円の半径と高さ
	float radius = std::sin(halfApexAngle) * length;
	float height = std::cos(halfApexAngle) * length;

	// 円の中心
	Vector3 localCenter(0.0f, -height, 0.0f);
	Vector3 worldCenter = anchor + rotation * localCenter;

	// 分割数
	const int32_t kDiv = 64;

	// ここでminAngle ～ maxAngleの弧だけ描く
	float startAngle = minAngle;
	float endAngle = maxAngle;
	float step = (endAngle - startAngle) / kDiv;

	float currentAngle = startAngle;
	Vector3 localP(radius * std::cos(angle), -height, radius * std::sin(currentAngle));
	Vector3 worldP = anchor + rotation * localP;

	Vector3 prev = worldP;

	for (int32_t i = 1; i <= kDiv; ++i) {

		currentAngle = startAngle + step * i;

		Vector3 localPi(radius * std::cos(currentAngle), -height, radius * std::sin(currentAngle));
		Vector3 worldPi = anchor + rotation * localPi;

		lineRenderer->Get3D()->DrawLine(prev, worldPi, Color::Yellow());
		prev = worldPi;
	}
}

void ConicalPendulum::ImGui() {

	ImGui::SeparatorText("Runtime");

	ImGui::Checkbox("isDrawDebug", &isDrawDebug);
	ImGui::Checkbox("isEditUpdate", &isEditUpdate);
	ImGui::Text("currentPos: (%.2f, %.2f, %.2f)", currentPos.x, currentPos.y, currentPos.z);
	ImGui::Text("angle: %.2f", easedAngle);
	ImGui::Text("reachCount: %d", reachCount);

	ImGui::SeparatorText("Edit");

	if (ImGui::Button("Reset")) {
		Init();
	}

	ImGui::DragFloat3("anchor", &anchor.x, 0.1f);
	ImGui::DragFloat("length", &length, 0.01f);
	ImGui::DragFloat("halfApexAngle", &halfApexAngle, 0.01f, 0.01f, pi / 2.0f);
	ImGui::DragFloat("moveSpeed", &moveSpeed, 0.01f);
	ImGui::DragFloat("minAngle", &minAngle, 0.01f, -pi * 2.0f, pi * 2.0f);
	ImGui::DragFloat("maxAngle", &maxAngle, 0.01f, -pi * 2.0f, pi * 2.0f);
	Easing::SelectEasingType(easingType);

	if (isEditUpdate) {

		Update();
	}
	if (isDrawDebug) {

		DrawDebugLine();
	}
}

void ConicalPendulum::FromJson(const Json& data) {

	if (data.empty()) {
		return;
	}

	anchor = SakuEngine::Vector3::FromJson(data.value("anchor", Json()));
	currentPos = SakuEngine::Vector3::FromJson(data.value("currentPos", Json()));
	length = data["length"];
	halfApexAngle = data["halfApexAngle"];
	angle = data["angle"];
	angularVelocity = data["angularVelocity"];
	moveSpeed = data.value("moveSpeed", 1.0f);
	minAngle = data["minAngle"];
	maxAngle = data["maxAngle"];
	easingType = SakuEngine::EnumAdapter<EasingType>::FromString(data.value("easingType", "Linear")).value();
}

void ConicalPendulum::ToJson(Json& data) {

	data["anchor"] = anchor.ToJson();
	data["currentPos"] = currentPos.ToJson();
	data["length"] = length;
	data["halfApexAngle"] = halfApexAngle;
	data["angle"] = angle;
	data["angularVelocity"] = angularVelocity;
	data["moveSpeed"] = moveSpeed;
	data["minAngle"] = minAngle;
	data["maxAngle"] = maxAngle;
	data["easingType"] = SakuEngine::EnumAdapter<EasingType>::ToString(easingType);
}
