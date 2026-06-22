#include "CameraControllerInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// imgui
#include <imgui.h>

// c++
#include <algorithm>
#include <string>

//============================================================================
//	CameraControllerInspectorDrawer classMethods
//============================================================================

namespace {

	// UUIDから表示名を作成する
	std::string MakeEntityTargetLabel(Engine::ECSWorld& world, Engine::UUID target) {

		if (!target) {
			return "なし";
		}

		const Engine::Entity entity = Engine::SceneObjectUtility::FindByLocalFileID(world, target);
		if (!entity.IsValid() || !world.IsAlive(entity)) {
			return "不明 : " + Engine::ToString(target);
		}

		if (world.HasComponent<Engine::NameComponent>(entity)) {
			return world.GetComponent<Engine::NameComponent>(entity).name;
		}
		return Engine::ToString(target);
	}

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
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return DrawModeField(draft.mode);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("編集中プレビュー", draft.editorPreview);
		});

	// 選択中のモードのパラメータだけを表示する、FollowLookAtは専用パラメータを表示する
	if (draft.mode == CameraControlMode::Follow) {
		if (MyGUI::CollapsingHeader("Follow")) {

			PushEditResult(DrawFollowSettings(world, draft.follow), anyItemActive);
		}
	} else if (draft.mode == CameraControlMode::LookAt) {
		if (MyGUI::CollapsingHeader("LookAt")) {

			PushEditResult(DrawLookAtSettings(world, draft.lookAt), anyItemActive);
		}
	} else if (draft.mode == CameraControlMode::FollowLookAt) {
		if (MyGUI::CollapsingHeader("Follow")) {

			PushEditResult(DrawFollowSettings(world, draft.followLookAt.follow), anyItemActive);
		}
		if (MyGUI::CollapsingHeader("LookAt")) {

			PushEditResult(DrawLookAtSettings(world, draft.followLookAt.lookAt), anyItemActive);
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

	ValueEditResult result{};
	if (!MyGUI::BeginPropertyRow(label)) {
		return result;
	}

	const float clearWidth = 56.0f;
	std::string buttonLabel = MakeEntityTargetLabel(world, target);
	ImGui::Button(buttonLabel.c_str(), ImVec2((std::max)(0.0f, ImGui::GetContentRegionAvail().x - clearWidth), 0.0f));
	result.anyItemActive |= ImGui::IsItemActive();

	if (ImGui::BeginDragDropTarget()) {

		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kHierarchyDragDropPayloadType);
		if (payload && payload->DataSize == sizeof(UUID)) {

			// ヒエラルキーのpayloadはrecords_のuuidなので、Edit/Playで安定するlocalFileIDへ変換して持つ
			const UUID droppedUUID = *static_cast<const UUID*>(payload->Data);
			const Entity dropped = world.FindByUUID(droppedUUID);
			UUID localFileID{};
			if (dropped.IsValid() && world.HasComponent<SceneObjectComponent>(dropped)) {
				localFileID = world.GetComponent<SceneObjectComponent>(dropped).localFileID;
			}
			if (localFileID && target != localFileID) {
				target = localFileID;
				result.valueChanged = true;
				result.editFinished = true;
			}
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::SameLine();
	if (ImGui::Button("クリア", ImVec2(clearWidth, 0.0f))) {
		target = UUID{};
		result.valueChanged = true;
		result.editFinished = true;
	}
	MyGUI::EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::CameraControllerInspectorDrawer::DrawFollowSettings(
	ECSWorld& world, CameraFollowSettings& settings) {

	ValueEditResult result{};
	Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("有効", settings.enabled));
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
		Accumulate(result, MyGUI::DragFloat("縦回転下限", settings.minPitchDegrees, { .dragSpeed = 0.1f, .minValue = -89.0f, .maxValue = 0.0f }));
		Accumulate(result, MyGUI::DragFloat("縦回転上限", settings.maxPitchDegrees, { .dragSpeed = 0.1f, .minValue = 0.0f, .maxValue = 89.0f }));
		Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("縦回転反転", settings.invertPitch));
	}
	return result;
}

Engine::ValueEditResult Engine::CameraControllerInspectorDrawer::DrawLookAtSettings(
	ECSWorld& world, CameraLookAtSettings& settings) {

	ValueEditResult result{};
	Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("有効", settings.enabled));
	Accumulate(result, DrawEntityTargetField("対象", world, settings.target));
	Accumulate(result, MyGUI::DragVector3("オフセット", settings.offset, { .dragSpeed = 0.01f }));
	Accumulate(result, MyGUI::DragFloat("回転補間", settings.rotationLerpSpeed, { .dragSpeed = 0.01f, .minValue = 0.0f }));
	Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("ロール固定", settings.lockRoll));
	return result;
}