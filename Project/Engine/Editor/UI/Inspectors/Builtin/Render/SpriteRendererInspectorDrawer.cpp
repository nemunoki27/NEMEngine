#include "SpriteRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

//============================================================================
//	SpriteRendererInspectorDrawer classMethods
//============================================================================

void Engine::SpriteRendererInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

	//============================================================================
	//	アセットファイル
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::AssetReferenceField("テクスチャ", draft.texture,
				context.editorContext->assetDatabase, { AssetType::Texture });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::AssetReferenceField("マテリアル", draft.material,
				context.editorContext->assetDatabase, { AssetType::Material });
			});
	}
	//============================================================================
	//	スプライト見た目パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector2("サイズ", draft.size,
				{ .dragSpeed = 0.1f, .minValue = 0.0f, .maxValue = 100000.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector2("ピボット", draft.pivot,
				{ .dragSpeed = 0.01f, .minValue = -1.0f, .maxValue = 1.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::ColorEdit("色", draft.color);
			});
	}
	//============================================================================
	//	スプライト描画パラメータ
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
			return InspectorDrawerCommon::DrawEnumComboField("ブレンド", draft.blendMode);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::InputText("キュー", draft.queue);
			});
	}
}
