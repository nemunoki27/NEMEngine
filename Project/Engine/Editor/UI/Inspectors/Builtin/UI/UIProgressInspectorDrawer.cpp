#include "UIProgressInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/Commands/Components/SetUIProgressDelayedCommand.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>

// c++
#include <algorithm>
#include <memory>

//============================================================================
//	UIProgressInspectorDrawer classMethods
//============================================================================

Engine::UIProgressInspectorDrawer::UIProgressInspectorDrawer()
	: SerializedComponentInspectorDrawer("UI Progress", "UIProgress") {
}

void Engine::UIProgressInspectorDrawer::DrawFields(
	const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(anyItemActive,
		[&]() { return InspectorDrawerCommon::DrawCheckboxField("編集中にプレビュー", draft.previewInEditMode); });
	// 範囲を変更したら現在値を収める
	const auto clampValue = [&]() {
		const float minValue = (std::min)(draft.minValue, draft.maxValue);
		const float maxValue = (std::max)(draft.minValue, draft.maxValue);
		draft.value = std::clamp(draft.value, minValue, maxValue);
	};
	DrawField(anyItemActive, [&]() {
		ValueEditResult result = MyGUI::DragFloat("最小値", draft.minValue, {.dragSpeed = 0.01f});
		if (result.valueChanged) {
			clampValue();
		}
		return result;
	});
	DrawField(anyItemActive, [&]() {
		ValueEditResult result = MyGUI::DragFloat("最大値", draft.maxValue, {.dragSpeed = 0.01f});
		if (result.valueChanged) {
			clampValue();
		}
		return result;
	});
	DrawField(anyItemActive, [&]() {
		const float minValue = (std::min)(draft.minValue, draft.maxValue);
		const float maxValue = (std::max)(draft.minValue, draft.maxValue);
		return MyGUI::DragFloat("現在値", draft.value,
			{.dragSpeed = 0.01f, .minValue = minValue, .maxValue = maxValue, .flags = ImGuiSliderFlags_AlwaysClamp});
	});
	DrawField(anyItemActive, [&]() {
		ValueEditResult result{};
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float buttonWidth = (std::max)(1.0f, (ImGui::GetContentRegionAvail().x - spacing) * 0.5f);
		if (ImGui::Button("最大にする", ImVec2(buttonWidth, 0.0f))) {
			draft.value = draft.maxValue;
			result.valueChanged = true;
			result.editFinished = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("最小にする", ImVec2(buttonWidth, 0.0f))) {
			draft.value = draft.minValue;
			result.valueChanged = true;
			result.editFinished = true;
		}
		return result;
	});
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("方向", draft.direction); });
	const auto* primitive = world.TryGetComponent<PrimitiveRendererComponent>(entity);
	const bool hasPrimitiveRenderer = primitive != nullptr;
	if (!hasPrimitiveRenderer) {
		ImGui::TextDisabled("Primitive Rendererが必要です");
	} else if (!IsPrimitiveScreen2D(*primitive)) {
		ImGui::TextDisabled("Screen2DのPlaneまたはRingが必要です");
	}

	ImGui::Indent();
	if (MyGUI::CollapsingHeader("補間")) {
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("補間する", draft.smooth); });
		if (draft.smooth) {
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat(
					"補間時間", draft.smoothDuration, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 60.0f});
			});
			DrawField(
				anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("イージング", draft.smoothEasing); });
		}
	}
	if (MyGUI::CollapsingHeader("遅延表示")) {

		const bool enableDisabled = !hasPrimitiveRenderer && !draft.delayed;
		if (enableDisabled) {
			ImGui::BeginDisabled();
		}
		const bool beforeDelayed = draft.delayed;
		const ValueEditResult result = InspectorDrawerCommon::DrawCheckboxField("遅延表示する", draft.delayed);
		if (enableDisabled) {
			ImGui::EndDisabled();
		}
		anyItemActive |= result.anyItemActive;
		if (result.valueChanged) {

			// 遅延表示に必要なRendererをCommandで更新する
			const bool executed = context.host && context.host->ExecuteEditorCommand(
													  std::make_unique<SetUIProgressDelayedCommand>(entity, draft.delayed));
			if (executed && world.IsAlive(entity) && world.HasComponent<UIProgressComponent>(entity)) {

				draft = world.GetComponent<UIProgressComponent>(entity);
			} else {
				draft.delayed = beforeDelayed;
			}
		}
		ImGui::Separator();
		if (draft.delayed) {
			DrawField(anyItemActive, [&]() {
				AssetEditSetting setting{};
				setting.graphicsCore = context.graphicsCore;
				return MyGUI::AssetReferenceField("遅延テクスチャ", draft.delayedTexture, context.editorContext->assetDatabase,
					{AssetType::Texture}, setting);
			});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat(
					"待機時間", draft.delayedWait, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 60.0f});
			});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat(
					"遅延時間", draft.delayedDuration, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 60.0f});
			});
			DrawField(anyItemActive,
				[&]() { return InspectorDrawerCommon::DrawEnumComboField("遅延イージング", draft.delayedEasing); });
		}
	}
	ImGui::Unindent();
	DrawField(
		anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("非スケール時間", draft.useUnscaledTime); });
}

void Engine::UIProgressInspectorDrawer::ApplyPreview(
	ECSWorld& world, const Entity& entity, const UIProgressComponent& previewComponent) {

	if (!world.IsAlive(entity) || !world.HasComponent<UIProgressComponent>(entity)) {
		return;
	}
	// 補間の実行状態を残して編集値を反映する
	ApplyUIProgressAuthoring(previewComponent, world.GetComponent<UIProgressComponent>(entity));
}
