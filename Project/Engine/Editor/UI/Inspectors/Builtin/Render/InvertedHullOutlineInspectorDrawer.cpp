#include "InvertedHullOutlineInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

//============================================================================
//	InvertedHullOutlineInspectorDrawer classMethods
//============================================================================

void Engine::InvertedHullOutlineInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	//============================================================================
	//	基本設定
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::ColorEdit("色", draft.color);
			});

		// ScreenPixelsのときは単位がpxであることをラベルで示す
		const bool screenPixels = draft.widthMode == OutlineWidthMode::ScreenPixels;
		DrawField(anyItemActive, [&]() {
			FloatEditSetting setting{};
			// 幅は負値を許容しない
			setting.minValue = 0.0f;
			setting.dragSpeed = screenPixels ? 0.1f : 0.001f;
			return MyGUI::DragFloat(screenPixels ? "膨張幅(px)" : "膨張幅", draft.width, setting);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("幅モード", draft.widthMode);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("膨張方式", draft.expansionMode);
			});
		DrawField(anyItemActive, [&]() {
			FloatEditSetting setting{};
			setting.dragSpeed = 0.001f;
			return MyGUI::DragFloat("Camera Z Offset", draft.cameraZOffset, setting);
			});
	}
	//============================================================================
	//	テクスチャ設定
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Baked Normalを使う", draft.useBakedNormal);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::AssetReferenceField("Baked Normal Texture", draft.bakedNormalTexture,
				context.editorContext->assetDatabase, { AssetType::Texture });
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("部位別幅を使う", draft.useOutlineSampler);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::AssetReferenceField("Outline Sampler", draft.outlineSamplerTexture,
				context.editorContext->assetDatabase, { AssetType::Texture });
			});
	}
	//============================================================================
	//	ステンシル設定
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Stencil抑制", draft.useStencil);
			});
	}
}
