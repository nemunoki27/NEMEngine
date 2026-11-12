#include "SkinnedAnimationInspectorDrawer.h"
#include <Engine/Editor/UI/Inspectors/Common/InspectorPresentation.h>
#include <Engine/Editor/UI/Inspectors/Common/TimeSourceFields.h>

//============================================================================
//	SkinnedAnimationInspectorDrawer classMethods
//============================================================================
void Engine::SkinnedAnimationInspectorDrawer::DrawFields(
	[[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&] {
		return DrawPlaybackStartFields(context, world, entity, draft.timeSource, draft.playInEditMode);
	});
	const SkinnedAnimationRuntimeData* runtime =
		TryGetSkinnedAnimationRuntime(world, entity);

	//============================================================================
	//	アニメーションの基本設定
	//============================================================================
	{
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
			return MyGUI::DragFloat(Engine::GetInspectorLabel(Engine::InspectorFieldID::PlaybackSpeed), draft.playbackSpeed,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f });
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField(Engine::GetInspectorLabel(Engine::InspectorFieldID::Loop), draft.loop);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("遷移時間", draft.transitionDuration,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 10.0f });
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("骨表示", draft.isDisplayBone);
			});
	}

	//============================================================================
	//	デバッグ表示
	//============================================================================
	{
		// Runtime情報は編集Draftへ混ぜず、現在のWorldから直接表示する
		MyGUI::Text("実行クリップ", runtime ? runtime->currentClip : "");
		MyGUI::TextFloat("再生時刻", runtime ? runtime->time : 0.0f);
		MyGUI::TextFloat("ブレンド時間", runtime ? runtime->blendTime : 0.0f);
		MyGUI::TextUInt("パレット数", runtime ? static_cast<uint32_t>(runtime->palette.size()) : 0u);
		const bool playing = runtime && runtime->initialized && draft.enabled &&
			(context.IsPlaying() || draft.playInEditMode) && !runtime->animationFinished;
		MyGUI::Text(GetInspectorLabel(InspectorFieldID::PlaybackState),
			GetInspectorPlaybackState(playing, false, runtime && runtime->animationFinished));
	}
}
