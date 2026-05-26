#include "SpotLightInspectorDrawer.h"

//============================================================================
//	SpotLightInspectorDrawer classMethods
//============================================================================

void Engine::SpotLightInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
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
			return MyGUI::DragVector3("方向", draft.direction, { .dragSpeed = 0.01f,.minValue = -1.0f,.maxValue = 1.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("強度", draft.intensity, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 128.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("距離", draft.distance, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1024.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("減衰", draft.decay, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 512.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("角度余弦", draft.cosAngle, { .dragSpeed = 0.01f,.minValue = -Math::pi,.maxValue = Math::pi });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("減衰開始余弦", draft.cosFalloffStart, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 8.0f });
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
			});
		DrawField(anyItemActive, [&]() {
			int32_t layerMask = static_cast<int32_t>(draft.affectLayerMask);
			auto result = MyGUI::DragInt("レイヤーマスク", layerMask, { .dragSpeed = 1,.minValue = 0,.maxValue = 0xfffffff });
			if (result.valueChanged) {

				draft.affectLayerMask = static_cast<uint32_t>(layerMask);
			}
			return result;
			});
	}
}

void Engine::SpotLightInspectorDrawer::OnBeforeCommit([[maybe_unused]] const SpotLightComponent& beforeComponent,
	SpotLightComponent& afterComponent) {

	// 方向ベクトルを正規化
	afterComponent.direction = Vector3::Normalize(afterComponent.direction);
}
