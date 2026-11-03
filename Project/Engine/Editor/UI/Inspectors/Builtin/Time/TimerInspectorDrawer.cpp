#include "TimerInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Inspectors/Common/TimeSourceFields.h>
#include <Engine/Editor/UI/ImGui/ImGuiEnum.h>

// c++
#include <limits>

void Engine::TimerInspectorDrawer::ApplyPreview(ECSWorld& world, const Entity& entity, const TimerComponent& component) {

	if (auto* current = world.TryGetComponent<TimerComponent>(entity)) {
		ApplyTimerSettings(component, *current);
		world.MarkComponentModified<TimerComponent>(entity);
	}
}

void Engine::TimerInspectorDrawer::DrawFields(const EditorPanelContext& context, ECSWorld& world,
	const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&] { return DrawTimeSourceField(context, world, entity, draft.timeSource); });
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField(
		GetInspectorLabel(InspectorFieldID::PlayOnStart), draft.playOnStart); });
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField(
		GetInspectorLabel(InspectorFieldID::PlayInEditMode), draft.playInEditMode); });
	DrawField(anyItemActive, [&] { return MyGUI::EnumCombo("計測方式", draft.mode); });
	if (draft.mode == TimerMode::Seconds) {
		DrawField(anyItemActive, [&] { return MyGUI::DragFloat("目標時間", draft.targetSeconds,
			{.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = std::numeric_limits<float>::max()}); });
	} else {
		DrawField(anyItemActive, [&] { return MyGUI::DragInt("目標フレーム数", draft.targetFrames,
			{.minValue = 0, .maxValue = std::numeric_limits<int>::max()}); });
	}
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("基準FPS", draft.baseFPS,
		{.dragSpeed = 1.0f, .minValue = 0.01f, .maxValue = 100000.0f}); });
	auto& current = world.GetComponent<TimerComponent>(entity);
	const bool playbackEnabled = context.IsPlaying() || draft.playInEditMode;
	const auto action = DrawInspectorPlaybackControls(playbackEnabled, true);
	if (action != InspectorPlaybackAction::None) current.runtimeRequest = static_cast<int>(action);
	MyGUI::TextDouble("現在時間（秒）", current.runtimeSeconds);
	MyGUI::TextInt("現在フレーム数", current.runtimeFrames);
	MyGUI::Text("目標到達", current.runtimeReached ? "到達" : "未到達");
	MyGUI::Text(GetInspectorLabel(InspectorFieldID::PlaybackState), GetInspectorPlaybackState(current.runtimePlaying, current.runtimePaused, false, true));
	if (!current.runtimeDiagnostic.empty()) MyGUI::Text("時間設定", current.runtimeDiagnostic);
}
