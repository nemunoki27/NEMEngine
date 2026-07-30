#include "SkinnedAnimationInspectorDrawer.h"

//============================================================================
//	SkinnedAnimationInspectorDrawer classMethods
//============================================================================
void Engine::SkinnedAnimationInspectorDrawer::DrawFields(
	[[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	const SkinnedAnimationRuntimeData* runtime =
		TryGetSkinnedAnimationRuntime(world, entity);

	//============================================================================
	//	アニメーションの基本設定
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
			});
		if (runtime && !runtime->availableClips.empty()) {
			DrawField(anyItemActive, [&]() {
				return MyGUI::StringCombo(
					"クリップ", draft.clip, runtime->availableClips, "<自動>");
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
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("骨表示", draft.isDisplayBone);
			});
	}

	ImGui::Separator();

	//============================================================================
	//	デバッグ表示
	//============================================================================
	{
		// Runtime情報は編集Draftへ混ぜず、現在のWorldから直接表示する
		ImGui::Text("実行クリップ : %s",
			runtime ? runtime->currentClip.c_str() : "");
		ImGui::Text("実行時間     : %.3f",
			runtime ? runtime->time : 0.0f);
		ImGui::Text("ブレンド時間 : %.3f",
			runtime ? runtime->blendTime : 0.0f);
		ImGui::Text("パレット数   : %u",
			runtime ? static_cast<uint32_t>(runtime->palette.size()) : 0u);
	}
}
