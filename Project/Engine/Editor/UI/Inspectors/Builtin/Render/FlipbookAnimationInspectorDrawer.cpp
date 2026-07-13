#include "FlipbookAnimationInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookTileLayout.h>

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
		return MyGUI::DragFloat("ループ間隔", draft.loopInterval,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 600.0f });
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
		return MyGUI::DragInt("分割Y", draft.tilesY, { .minValue = 1,.maxValue = 256 });
		});
	NormalizeFlipbookTileLayout(draft.tilesX, draft.tilesY);
	// 縦の分割数だけXタイル数を設定する
	for (int32_t index = 0; index < draft.tilesY; ++index) {

		ImGui::PushID(index);
		DrawField(anyItemActive, [&]() {
			ValueEditResult result = MyGUI::DragInt("分割X", draft.tilesX[index],
				{ .minValue = 1, .maxValue = 16384 });
			draft.tilesX[index] = (std::max)(draft.tilesX[index], 1);
			return result;
			});
		ImGui::PopID();
	}
	DrawField(anyItemActive, [&]() {
		return MyGUI::EnumCombo("イージング", draft.easingType);
		});
}
