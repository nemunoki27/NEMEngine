#include "DirectionalLightInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

//============================================================================
//	DirectionalLightInspectorDrawer classMethods
//============================================================================
void Engine::DirectionalLightInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
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
			return MyGUI::DragFloat("影の強さ", draft.shadowStrength, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1.0f });
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawLayerMaskField("レイヤーマスク", draft.affectLayerMask);
			});
	}
}

void Engine::DirectionalLightInspectorDrawer::OnBeforeCommit([[maybe_unused]] const DirectionalLightComponent& beforeComponent,
	DirectionalLightComponent& afterComponent) {

	// 方向ベクトルを正規化
	afterComponent.direction = Vector3::Normalize(afterComponent.direction);
}
