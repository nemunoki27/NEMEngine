#include "SceneViewCameraController.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <array>
#include <string>

using namespace Engine;
using namespace Engine::SceneViewCameraDefaults;
using namespace Engine::SceneViewCameraNavigationUtility;

void Engine::SceneViewCameraController::DrawNavigationControls() {

	if (MyGUI::EnumCombo("操作方式", settings_.controlStyle).valueChanged) {
		// 方式を切り替えてもオリジナルの割当は残す
		CancelNavigation();
		focusActive_ = false;
	}
	if (settings_.controlStyle == SceneViewCameraControlStyle::Original) {
		ImGui::Indent();
		for (size_t index = 0; index < settings_.customControls.size(); ++index) {
			const auto action = static_cast<SceneViewCameraAction>(index);
			std::array<ImGuiUtility::EnumOptionState, EnumAdapter<SceneViewCameraControlSource>::GetEnumCount()> options;
			std::array<std::string, EnumAdapter<SceneViewCameraControlSource>::GetEnumCount()> reasons;
			for (size_t candidate = 0; candidate < options.size(); ++candidate) {
				const auto source = EnumAdapter<SceneViewCameraControlSource>::GetValue(static_cast<uint32_t>(candidate));
				if (!SupportsAction(action, source)) {
					reasons[candidate] = "対応する操作なし";
				} else {
					auto controls = settings_.customControls;
					controls[index] = source;
					if (const auto conflict = FindConflict(controls)) {
						const auto other = conflict->first == action ? conflict->second : conflict->first;
						reasons[candidate] = std::string(GetActionLabel(other)) + "と重複";
					}
				}
				options[candidate] = {reasons[candidate].empty(), reasons[candidate].empty() ? nullptr : reasons[candidate].c_str()};
			}
			if (MyGUI::EnumCombo(GetActionLabel(action), settings_.customControls[index], {}, options).valueChanged) {
				CancelNavigation();
			}
		}
		ImGui::Unindent();
	}

	// 選択中の割当をその場で確認できるようにする
	if (ImGui::TreeNode("操作方法")) {
		const auto controls = ResolveControls(settings_.controlStyle, settings_.customControls);
		for (size_t index = 0; index < controls.size(); ++index) {
			const auto action = static_cast<SceneViewCameraAction>(index);
			MyGUI::Text(GetActionLabel(action), GetControlDescription(action, controls[index]));
		}
		MyGUI::Text("対象に寄る", "選択してF / ダブルクリック");
		ImGui::TreePop();
	}
}

void Engine::SceneViewCameraController::DrawEditorTool([[maybe_unused]] const EditorToolContext& context) {

	if (!openWindow_) { return; }
	if (!ImGui::Begin(MakeWindowName("SceneViewCamera").c_str(), &openWindow_)) {
		ImGui::End();
		return;
	}

	ImGui::SeparatorText("2D");
	{
		ImGui::PushID("2Dシーンビューカメラ");
		MyGUI::ScopedPropertyLabelWidth labels("SceneCamera2D");
		if (ImGui::Button("カメラ設定をデフォルトに戻す")) {
			settings_.Reset2D();
			CancelNavigation();
		}
		Vector2 position(settings_.cameraState.transform2D.pos.x, settings_.cameraState.transform2D.pos.y);
		if (MyGUI::DragVector2("位置", position, {.dragSpeed = 1.0f}).valueChanged) {
			settings_.cameraState.transform2D.pos.x = position.x;
			settings_.cameraState.transform2D.pos.y = position.y;
		}
		MyGUI::DragFloat("ズーム", settings_.cameraState.orthographicZoom, {.minValue = kMin2DZoom, .maxValue = kMax2DZoom});
		MyGUI::DragFloat("ニアクリップ", settings_.cameraState.orthoNearClip, {.dragSpeed = 0.1f});
		MyGUI::DragFloat("ファークリップ", settings_.cameraState.orthoFarClip, {.dragSpeed = 1.0f});
		MyGUI::DragInt("描画レイヤー", settings_.cameraState.orthographicCullingMask);
		MyGUI::DragFloat("移動速度", settings_.panSpeed2D, {.minValue = 0.0f, .maxValue = 100.0f});
		MyGUI::DragFloat("ズーム速度", settings_.zoomRate2D, {.minValue = 0.0f, .maxValue = 10.0f});
		ImGui::PopID();
	}

	ImGui::SeparatorText("3D");
	{
		ImGui::PushID("3Dシーンビューカメラ");
		MyGUI::ScopedPropertyLabelWidth labels("SceneCamera3D");
		if (ImGui::Button("カメラ設定をデフォルトに戻す")) {
			settings_.Reset3D();
			focusActive_ = false;
			navigationState_ = {};
			CancelNavigation();
		}
		bool poseChanged = MyGUI::DragVector3("位置", settings_.cameraState.transform3D.pos, {.dragSpeed = 0.1f}).valueChanged;
		poseChanged |= MyGUI::DragVector3("回転", settings_.cameraState.transform3D.rotation, {.dragSpeed = 0.1f}).valueChanged;
		if (poseChanged) {
			focusActive_ = false;
			navigationState_ = {};
			CancelNavigation();
		}
		MyGUI::DragFloat("垂直画角", settings_.cameraState.perspectiveFovY, {.dragSpeed = 0.1f, .minValue = 1.0f, .maxValue = 179.0f});
		MyGUI::DragFloat("近クリップ", settings_.cameraState.perspectiveNearClip, {.minValue = 0.001f, .maxValue = 1000000.0f});
		MyGUI::DragFloat("遠クリップ", settings_.cameraState.perspectiveFarClip, {.dragSpeed = 1.0f, .minValue = 0.002f, .maxValue = 10000000.0f});
		MyGUI::DragInt("描画レイヤー", settings_.cameraState.perspectiveCullingMask);

		ImGui::SeparatorText("操作設定");
		DrawNavigationControls();
		MyGUI::DragFloat("移動速度", settings_.moveSpeed, {.dragSpeed = 0.1f, .minValue = kMinMoveSpeed, .maxValue = kMaxMoveSpeed});
		MyGUI::DragFloat("回転速度", settings_.rotateSpeed, {.dragSpeed = 0.0001f, .minValue = 0.0f, .maxValue = 1.0f});
		MyGUI::DragFloat("ズーム速度", settings_.zoomRate, {.minValue = 0.0f, .maxValue = 100.0f});
		MyGUI::DragFloat("パン速度", settings_.panSpeed, {.dragSpeed = 0.001f, .minValue = 0.0f, .maxValue = 10.0f});
		ImGui::PopID();
	}
	SceneViewCameraSettingsUtility::Sanitize(settings_);
	ImGui::End();
}
