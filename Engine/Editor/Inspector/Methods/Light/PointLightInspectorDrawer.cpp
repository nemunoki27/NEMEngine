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
			return MyGUI::ColorEdit("Color", draft.color);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("Intensity", draft.intensity, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 128.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("Radius", draft.radius, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 512.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("Decay", draft.decay, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 512.0f });
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Enabled", draft.enabled);
			});
		DrawField(anyItemActive, [&]() {
			int32_t layerMask = static_cast<int32_t>(draft.affectLayerMask);
			auto result = MyGUI::DragInt("Layer Mask", layerMask, { .dragSpeed = 1,.minValue = 0,.maxValue = 0xfffffff });
			if (result.valueChanged) {

				draft.affectLayerMask = static_cast<uint32_t>(layerMask);
			}
			return result;
			});
	}
}