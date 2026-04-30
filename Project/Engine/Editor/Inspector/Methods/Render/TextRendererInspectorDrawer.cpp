#include "TextRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Methods/SetSerializedComponentCommand.h>
#include <Engine/Editor/Panel/Interface/IEditorPanelHost.h>
#include <Engine/Editor/Inspector/Methods/Common/InspectorDrawerCommon.h>
#include <Engine/Utility/ImGui/MyGUI.h>

//============================================================================
//	TextRendererInspectorDrawer classMethods
//============================================================================

void Engine::TextRendererInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

	//============================================================================
	//	アセットファイル
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::AssetReferenceField("Font", draft.font,
				context.editorContext->assetDatabase, { AssetType::Font });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::AssetReferenceField("Material", draft.material,
				context.editorContext->assetDatabase, { AssetType::Material });
			});
	}
	//============================================================================
	//	テキスト見た目パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::InputText("Text", draft.text, { .multiLine = true });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("Font Size", draft.fontSize,
				{ .dragSpeed = 0.1f, .minValue = 0.0f, .maxValue = 10000.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("Char Spacing", draft.charSpacing,
				{ .dragSpeed = 0.01f, .minValue = -1000.0f, .maxValue = 1000.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::ColorEdit("Color4", draft.color);
			});
	}
	//============================================================================
	//	テキスト描画パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("Layer", draft.layer);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("Order", draft.order);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Visible", draft.visible);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("Blend Mode", draft.blendMode);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::InputText("Queue", draft.queue);
			});
	}
}
