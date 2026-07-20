#include "IrisTransitionInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>

//============================================================================
//	IrisTransitionInspectorDrawer internal
//============================================================================
namespace {

	Engine::ValueEditResult DrawSliderFloat(const char* label,
		float& value, float minValue, float maxValue) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		result.valueChanged = ImGui::SliderFloat(
			"##Value", &value, minValue, maxValue, "%.3f",
			ImGuiSliderFlags_AlwaysClamp);
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();

		Engine::MyGUI::EndPropertyRow();
		return result;
	}
}

//============================================================================
//	IrisTransitionInspectorDrawer classMethods
//============================================================================
void Engine::IrisTransitionInspectorDrawer::DrawFields(
	const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	applyEditPreview_ = !context.IsPlaying();
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"編集中にプレビュー", draft.previewInEditMode);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragVector2("画面座標", draft.screenPosition,
			{ .dragSpeed = 1.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("色", draft.transitionColor);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("境界のぼかし", draft.edgeSoftness,
			{ .dragSpeed = 0.1f,.minValue = 0.0f,.maxValue = 1000.0f });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"マスクを反転", draft.invertMask);
		});

	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"入力を停止させる", draft.blockInput);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"シーン遷移後自動アイリスイン",
			draft.autoIrisInAfterSceneTransition);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"非スケール時間", draft.useUnscaledTime);
		});

	ImGui::Indent();
	if (MyGUI::CollapsingHeader("アイリスアウト")) {
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("時間", draft.irisOutDuration,
				{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 60.0f });
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField(
				"イージング", draft.irisOutEasing);
			});
	}
	if (MyGUI::CollapsingHeader("アイリスイン")) {
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("時間", draft.irisInDuration,
				{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 60.0f });
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField(
				"イージング", draft.irisInEasing);
			});
	}
	ImGui::Unindent();

	ImGui::Indent();
	if (MyGUI::CollapsingHeader("プレビュー")) {
		DrawField(anyItemActive, [&]() {
			return DrawSliderFloat("進行度", draft.previewProgress, 0.0f, 1.0f);
			});

		const bool previewDisabled =
			!context.IsPlaying() && !draft.previewInEditMode;
		if (previewDisabled) {
			ImGui::BeginDisabled();
		}
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float buttonWidth = (std::max)(
			1.0f, (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f);
		if (ImGui::Button("アイリスアウト##Preview", ImVec2(buttonWidth, 0.0f))) {
			world.GetComponent<IrisTransitionComponent>(entity).IrisOut();
		}
		ImGui::SameLine();
		if (ImGui::Button("アイリスイン##Preview", ImVec2(buttonWidth, 0.0f))) {
			world.GetComponent<IrisTransitionComponent>(entity).IrisIn();
		}
		ImGui::SameLine();
		if (ImGui::Button("リセット", ImVec2(buttonWidth, 0.0f))) {
			world.GetComponent<IrisTransitionComponent>(entity).Reset();
		}
		if (previewDisabled) {
			ImGui::EndDisabled();
		}
	}
	ImGui::Unindent();
}

void Engine::IrisTransitionInspectorDrawer::ApplyPreview(ECSWorld& world,
	const Entity& entity, const IrisTransitionComponent& previewComponent) {

	if (!world.IsAlive(entity) || !world.HasComponent<IrisTransitionComponent>(entity)) {
		return;
	}
	auto& component = world.GetComponent<IrisTransitionComponent>(entity);
	ApplyIrisTransitionAuthoring(previewComponent, component);
	if (applyEditPreview_) {
		component.RequestEditPreview();
	}
}
