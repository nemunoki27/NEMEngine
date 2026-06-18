#include "PointLightInspectorDrawer.h"

//============================================================================
//	PointLightInspectorDrawer classMethods
//============================================================================
void Engine::PointLightInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

	//============================================================================
	//	ライトパラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::ColorEdit("色", draft.color);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("強度", draft.intensity, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 128.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("半径", draft.radius, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 512.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("減衰", draft.decay, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 512.0f });
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawLayerMaskField("レイヤーマスク", draft.affectLayerMask);
			});
	}
}
