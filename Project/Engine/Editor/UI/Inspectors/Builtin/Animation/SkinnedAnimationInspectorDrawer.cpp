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
			return InspectorDrawerCommon::DrawCheckboxField("Enabled", draft.enabled);
			});
		if (!draft.runtimeAvailableClips.empty()) {
			DrawField(anyItemActive, [&]() {
				return MyGUI::StringCombo("Clip", draft.clip, draft.runtimeAvailableClips, "<Auto>");
				});
		} else {
			DrawField(anyItemActive, [&]() {
				return MyGUI::InputText("Clip", draft.clip);
				});
		}
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Loop", draft.loop);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Play In Edit", draft.playInEditMode);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("Playback Speed", draft.playbackSpeed,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("Transition", draft.transitionDuration,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 10.0f });
			});
	}

	ImGui::Separator();

	//============================================================================
	//	デバッグ表示
	//============================================================================
	{
		ImGui::Text("Runtime Clip : %s", draft.runtimeCurrentClip.c_str());
		ImGui::Text("Runtime Time : %.3f", draft.runtimeTime);
		ImGui::Text("Blend Time   : %.3f", draft.runtimeBlendTime);
		ImGui::Text("Palette Size : %u", static_cast<uint32_t>(draft.palette.size()));
	}
}