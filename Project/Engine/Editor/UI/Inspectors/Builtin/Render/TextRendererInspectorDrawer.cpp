#include "TextRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Components/SetSerializedComponentCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

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
			return MyGUI::AssetReferenceField("フォント", draft.font,
				context.editorContext->assetDatabase, { AssetType::Font });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::AssetReferenceField("マテリアル", draft.material,
				context.editorContext->assetDatabase, { AssetType::Material });
			});
	}
	//============================================================================
	//	テキスト見た目パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::InputText("テキスト", draft.text, { .multiLine = true });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("フォントサイズ", draft.fontSize,
				{ .dragSpeed = 0.1f, .minValue = 0.0f, .maxValue = 10000.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("文字間隔", draft.charSpacing,
				{ .dragSpeed = 0.01f, .minValue = -1000.0f, .maxValue = 1000.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::ColorEdit("色", draft.color);
			});
	}
	//============================================================================
	//	テキスト描画パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("レイヤー", draft.layer);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("描画順", draft.order);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("表示", draft.visible);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("ブレンドモード", draft.blendMode);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("キュー", draft.queue);
			});
	}
}
