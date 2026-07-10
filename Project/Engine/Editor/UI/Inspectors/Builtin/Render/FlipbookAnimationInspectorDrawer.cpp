#include "FlipbookAnimationInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

//============================================================================
//	FlipbookAnimationInspectorDrawer classMethods
//============================================================================
void Engine::FlipbookAnimationInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	// 再生設定
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("ループ再生", draft.loop);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("編集中も再生", draft.playInEditMode);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("再生終了後に非表示", draft.endAnimUnDisplay);
		});

	// アニメーション設定
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("再生時間", draft.duration, { .dragSpeed = 0.01f,.minValue = 0.001f,.maxValue = 600.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragInt("分割X", draft.tilesX, { .minValue = 1,.maxValue = 256 });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragInt("分割Y", draft.tilesY, { .minValue = 1,.maxValue = 256 });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::EnumCombo("イージング", draft.easingType);
		});
}
