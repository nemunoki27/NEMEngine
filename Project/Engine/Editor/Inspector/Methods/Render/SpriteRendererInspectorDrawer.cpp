#include "SpriteRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/InspectorDrawerCommon.h>
#include <Engine/Utility/ImGui/MyGUI.h>

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
			return MyGUI::AssetReferenceField("Texture", draft.texture,
				context.editorContext->assetDatabase, { AssetType::Texture });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::AssetReferenceField("Material", draft.material,
				context.editorContext->assetDatabase, { AssetType::Material });
			});
	}
	//============================================================================
	//	スプライト見た目パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector2("Size", draft.size,
				{ .dragSpeed = 0.1f, .minValue = 0.0f, .maxValue = 100000.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector2("Pivot", draft.pivot,
				{ .dragSpeed = 0.01f, .minValue = -1.0f, .maxValue = 1.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::ColorEdit("Color4", draft.color);
			});
	}
	//============================================================================
	//	スプライト描画パラメータ
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