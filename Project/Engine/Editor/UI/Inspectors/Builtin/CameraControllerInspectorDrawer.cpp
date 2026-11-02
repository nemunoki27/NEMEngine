#include "CameraControllerInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <string>

#include <imgui.h>

//============================================================================
//	CameraControllerInspectorDrawer classMethods
//============================================================================

namespace {

	// 編集結果を集約する
	void Accumulate(Engine::ValueEditResult& result, const Engine::ValueEditResult& item) {

		result.valueChanged |= item.valueChanged;
		result.anyItemActive |= item.anyItemActive;
		result.editFinished |= item.editFinished;
	}
}

void Engine::CameraControllerInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	// CameraController全体の設定
	DrawField(anyItemActive, [&]() {
		return DrawModeField(draft.mode);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(Engine::GetInspectorLabel(Engine::InspectorFieldID::EditorPreview), draft.editorPreview);
		});

	// 選択中のモードのパラメータだけを表示する、FollowLookAtは専用パラメータを表示する
	if (draft.mode == CameraControlMode::Follow) {
		InspectorIndentScope section("Follow");
		if (MyGUI::CollapsingHeader("Follow")) {

			PushEditResult(DrawFollowSettings(world, draft.follow), anyItemActive);
		}
	} else if (draft.mode == CameraControlMode::LookAt) {
		InspectorIndentScope section("LookAt");
		if (MyGUI::CollapsingHeader("LookAt")) {

			PushEditResult(DrawLookAtSettings(world, draft.lookAt), anyItemActive);
		}
	} else if (draft.mode == CameraControlMode::FollowLookAt) {
		{
			InspectorIndentScope section("Follow");
			if (MyGUI::CollapsingHeader("Follow")) {

				PushEditResult(DrawFollowSettings(world, draft.followLookAt.follow), anyItemActive);
			}
		}
		{
			InspectorIndentScope section("LookAt");
			if (MyGUI::CollapsingHeader("LookAt")) {

				PushEditResult(DrawLookAtSettings(world, draft.followLookAt.lookAt), anyItemActive);
			}
		}
	}
}

void Engine::CameraControllerInspectorDrawer::OnBeforeCommit(
	[[maybe_unused]] const CameraControllerComponent& beforeComponent, CameraControllerComponent& afterComponent) {

	// 入力値を実行時に扱いやすい範囲へ収める
	afterComponent.follow.posLerpSpeed = (std::max)(0.0f, afterComponent.follow.posLerpSpeed);
	afterComponent.lookAt.rotationLerpSpeed = (std::max)(0.0f, afterComponent.lookAt.rotationLerpSpeed);
	afterComponent.followLookAt.follow.posLerpSpeed = (std::max)(0.0f, afterComponent.followLookAt.follow.posLerpSpeed);
	afterComponent.followLookAt.lookAt.rotationLerpSpeed = (std::max)(0.0f, afterComponent.followLookAt.lookAt.rotationLerpSpeed);
}

Engine::ValueEditResult Engine::CameraControllerInspectorDrawer::DrawModeField(CameraControlMode& mode) {

	return InspectorDrawerCommon::DrawEnumComboField("モード", mode);
}

Engine::ValueEditResult Engine::CameraControllerInspectorDrawer::DrawEntityTargetField(
	const char* label, ECSWorld& world, UUID& target) {

	return SceneEntityReferenceFields::DrawLocal(label, world, target);
}

Engine::ValueEditResult Engine::CameraControllerInspectorDrawer::DrawFollowSettings(
	ECSWorld& world, CameraFollowSettings& settings) {

	ValueEditResult result{};
	Accumulate(result, InspectorDrawerCommon::DrawCheckboxField(Engine::GetInspectorLabel(Engine::InspectorFieldID::Enabled), settings.enabled));
	Accumulate(result, DrawEntityTargetField("対象", world, settings.target));
	Accumulate(result, MyGUI::DragVector3("オフセット", settings.offset, { .dragSpeed = 0.01f }));
	Accumulate(result, MyGUI::DragVector3("軸マスク", settings.axisMask, { .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f }));
	Accumulate(result, MyGUI::DragFloat("位置補間", settings.posLerpSpeed, { .dragSpeed = 0.01f, .minValue = 0.0f }));

	// 入力でのオービット回転
	Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("入力で回転", settings.enableInputRotation));
	if (settings.enableInputRotation) {

		Accumulate(result, MyGUI::DragFloat("入力補間", settings.inputLerpRate, { .dragSpeed = 0.01f, .minValue = 0.0f }));
		Accumulate(result, MyGUI::DragVector2("パッド感度", settings.padSensitivity, { .dragSpeed = 0.01f, .minValue = 0.0f }));
		Accumulate(result, MyGUI::DragVector2("マウス感度", settings.mouseSensitivity, { .dragSpeed = 0.001f, .minValue = 0.0f }));
		Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("パッド操作有効", settings.padEnabled));
		Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("マウス操作有効", settings.mouseEnabled));
		Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("操作から自動で入力を設定", settings.autoInputDevice));
		Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("縦回転反転", settings.invertPitch));
	}
	return result;
}

Engine::ValueEditResult Engine::CameraControllerInspectorDrawer::DrawLookAtSettings(
	ECSWorld& world, CameraLookAtSettings& settings) {

	ValueEditResult result{};
	Accumulate(result, InspectorDrawerCommon::DrawCheckboxField(Engine::GetInspectorLabel(Engine::InspectorFieldID::Enabled), settings.enabled));
	Accumulate(result, DrawEntityTargetField("対象", world, settings.target));
	Accumulate(result, MyGUI::DragVector3("オフセット", settings.offset, { .dragSpeed = 0.01f }));
	Accumulate(result, MyGUI::DragFloat("回転補間", settings.rotationLerpSpeed, { .dragSpeed = 0.01f, .minValue = 0.0f }));
	Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("ロール固定", settings.lockRoll));
	return result;
}
