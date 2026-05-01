#include "CameraControllerInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Editor/Inspector/Methods/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>
#include <Engine/Utility/ImGui/MyGUI.h>

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
			return "None";
		}

		const Engine::Entity entity = world.FindByUUID(target);
		if (!entity.IsValid() || !world.IsAlive(entity)) {
			return "Missing : " + Engine::ToString(target);
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

	// Boundsの最小最大を正しい順番へ整える
	void NormalizeBounds(Engine::Vector3& minValue, Engine::Vector3& maxValue) {

		if (maxValue.x < minValue.x) {
			std::swap(minValue.x, maxValue.x);
		}
		if (maxValue.y < minValue.y) {
			std::swap(minValue.y, maxValue.y);
		}
		if (maxValue.z < minValue.z) {
			std::swap(minValue.z, maxValue.z);
		}
	}
}

void Engine::CameraControllerInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	// CameraController全体の設定
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("Enabled", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return DrawModeField(draft.mode);
		});

	if (MyGUI::CollapsingHeader("Follow")) {
		PushEditResult(DrawFollowSettings(world, draft.follow), anyItemActive);
	}
	if (MyGUI::CollapsingHeader("LookAt")) {
		PushEditResult(DrawLookAtSettings(world, draft.lookAt), anyItemActive);
	}
	if (MyGUI::CollapsingHeader("Shake")) {
		PushEditResult(DrawShakeSettings(draft.shake), anyItemActive);
	}
}

void Engine::CameraControllerInspectorDrawer::OnBeforeCommit(
	[[maybe_unused]] const CameraControllerComponent& beforeComponent, CameraControllerComponent& afterComponent) {

	// 入力値を実行時に扱いやすい範囲へ収める
	afterComponent.follow.positionLerpSpeed = (std::max)(0.0f, afterComponent.follow.positionLerpSpeed);
	afterComponent.lookAt.rotationLerpSpeed = (std::max)(0.0f, afterComponent.lookAt.rotationLerpSpeed);
	afterComponent.shake.amplitude = (std::max)(0.0f, afterComponent.shake.amplitude);
	afterComponent.shake.duration = (std::max)(0.0f, afterComponent.shake.duration);
	afterComponent.shake.frequency = (std::max)(0.0f, afterComponent.shake.frequency);
	afterComponent.shake.damping = (std::max)(0.0f, afterComponent.shake.damping);
	NormalizeBounds(afterComponent.follow.boundsMin, afterComponent.follow.boundsMax);
}

Engine::ValueEditResult Engine::CameraControllerInspectorDrawer::DrawModeField(CameraControlMode& mode) {

	ValueEditResult result{};
	if (!MyGUI::BeginPropertyRow("Mode")) {
		return result;
	}

	constexpr CameraControlMode kModes[] = {
		CameraControlMode::None,
		CameraControlMode::Follow,
		CameraControlMode::LookAt,
		CameraControlMode::FollowLookAt,
	};

	if (ImGui::BeginCombo("##Value", ToString(mode))) {
		for (CameraControlMode candidate : kModes) {

			const bool selected = candidate == mode;
			if (ImGui::Selectable(ToString(candidate), selected)) {
				mode = candidate;
				result.valueChanged = true;
				result.editFinished = true;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	result.anyItemActive = ImGui::IsItemActive();
	MyGUI::EndPropertyRow();
	return result;
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

			const UUID droppedUUID = *static_cast<const UUID*>(payload->Data);
			if (target != droppedUUID) {
				target = droppedUUID;
				result.valueChanged = true;
				result.editFinished = true;
			}
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::SameLine();
	if (ImGui::Button("Clear", ImVec2(clearWidth, 0.0f))) {
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
	Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("Enabled", settings.enabled));
	Accumulate(result, DrawEntityTargetField("Target", world, settings.target));
	Accumulate(result, MyGUI::DragVector3("Offset", settings.offset, { .dragSpeed = 0.01f }));
	Accumulate(result, MyGUI::DragVector3("Axis Mask", settings.axisMask, { .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f }));
	Accumulate(result, MyGUI::DragFloat("Position Lerp", settings.positionLerpSpeed, { .dragSpeed = 0.01f, .minValue = 0.0f }));
	Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("Use Bounds", settings.useBounds));
	if (settings.useBounds) {
		Accumulate(result, MyGUI::DragVector3("Bounds Min", settings.boundsMin, { .dragSpeed = 0.1f }));
		Accumulate(result, MyGUI::DragVector3("Bounds Max", settings.boundsMax, { .dragSpeed = 0.1f }));
	}
	return result;
}

Engine::ValueEditResult Engine::CameraControllerInspectorDrawer::DrawLookAtSettings(
	ECSWorld& world, CameraLookAtSettings& settings) {

	ValueEditResult result{};
	Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("Enabled", settings.enabled));
	Accumulate(result, DrawEntityTargetField("Target", world, settings.target));
	Accumulate(result, MyGUI::DragVector3("Offset", settings.offset, { .dragSpeed = 0.01f }));
	Accumulate(result, MyGUI::DragFloat("Rotation Lerp", settings.rotationLerpSpeed, { .dragSpeed = 0.01f, .minValue = 0.0f }));
	Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("Lock Roll", settings.lockRoll));
	return result;
}

Engine::ValueEditResult Engine::CameraControllerInspectorDrawer::DrawShakeSettings(CameraShakeSettings& settings) {

	ValueEditResult result{};
	Accumulate(result, InspectorDrawerCommon::DrawCheckboxField("Enabled", settings.enabled));
	Accumulate(result, MyGUI::DragFloat("Amplitude", settings.amplitude, { .dragSpeed = 0.01f, .minValue = 0.0f }));
	Accumulate(result, MyGUI::DragFloat("Duration", settings.duration, { .dragSpeed = 0.01f, .minValue = 0.0f }));
	Accumulate(result, MyGUI::DragFloat("Frequency", settings.frequency, { .dragSpeed = 0.01f, .minValue = 0.0f }));
	Accumulate(result, MyGUI::DragFloat("Damping", settings.damping, { .dragSpeed = 0.01f, .minValue = 0.0f }));
	Accumulate(result, MyGUI::DragVector3("Axis Mask", settings.axisMask, { .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f }));
	return result;
}