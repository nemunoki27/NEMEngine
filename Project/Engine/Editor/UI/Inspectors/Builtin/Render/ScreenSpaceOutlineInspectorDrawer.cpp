#include "ScreenSpaceOutlineInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineConstants.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>

//============================================================================
//	ScreenSpaceOutlineInspectorDrawer classMethods
//============================================================================
void Engine::ScreenSpaceOutlineInspectorDrawer::DrawFields(
	[[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("色", draft.color);
		});
	DrawField(anyItemActive, [&]() {
		FloatEditSetting setting{};
		// 幅は[0,上限px]に固定する、巨大値はDilationのGPU Hang原因になる
		setting.minValue = 0.0f;
		setting.maxValue = static_cast<float>(kMaxScreenSpaceOutlineRadiusPixels);
		setting.dragSpeed = 0.1f;
		return MyGUI::DragFloat("幅(px)", draft.widthPixels, setting);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragInt("優先順位", draft.priority);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawEnumComboField("表示方式", draft.visibilityMode);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawEnumComboField("領域方式", draft.regionMode);
		});
	const PrimitiveRendererComponent* primitive =
		world.TryGetComponent<PrimitiveRendererComponent>(entity);
	const bool supportsAlphaSource =
		world.TryGetComponent<SpriteRendererComponent>(entity) != nullptr ||
		(primitive && IsPrimitiveScreen2D(*primitive));
	if (supportsAlphaSource) {
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("Alpha判定元", draft.alphaSource);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("UI重なり", draft.uiOcclusionMode);
			});
	}
}
