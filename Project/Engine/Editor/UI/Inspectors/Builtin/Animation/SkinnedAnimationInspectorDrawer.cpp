#include "SkinnedAnimationInspectorDrawer.h"

//============================================================================
//	SkinnedAnimationInspectorDrawer classMethods
//============================================================================
void Engine::SkinnedAnimationInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	//============================================================================
	//	アニメーションの基本設定
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
			});
		if (!draft.runtimeAvailableClips.empty()) {
			DrawField(anyItemActive, [&]() {
				return MyGUI::StringCombo("クリップ", draft.clip, draft.runtimeAvailableClips, "<自動>");
				});
		} else {
			DrawField(anyItemActive, [&]() {
				return MyGUI::InputText("クリップ", draft.clip);
				});
		}
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("ループ", draft.loop);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("編集中に再生", draft.playInEditMode);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("再生速度", draft.playbackSpeed,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("遷移時間", draft.transitionDuration,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 10.0f });
			});
	}

	ImGui::Separator();

	//============================================================================
	//	デバッグ表示
	//============================================================================
	{
		ImGui::Text("実行クリップ : %s", draft.runtimeCurrentClip.c_str());
		ImGui::Text("実行時間     : %.3f", draft.runtimeTime);
		ImGui::Text("ブレンド時間 : %.3f", draft.runtimeBlendTime);
		ImGui::Text("パレット数   : %u", static_cast<uint32_t>(draft.palette.size()));
	}
}
