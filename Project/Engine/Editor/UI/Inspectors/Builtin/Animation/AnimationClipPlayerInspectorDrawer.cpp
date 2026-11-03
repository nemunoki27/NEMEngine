#include "AnimationClipPlayerInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Inspectors/Common/TimeSourceFields.h>
#include <Engine/Editor/UI/ImGui/ImGuiEnum.h>
#include <Engine/Editor/Core/EditorContext.h>

void Engine::AnimationClipPlayerInspectorDrawer::ApplyPreview(ECSWorld& world, const Entity& entity,
	const AnimationClipPlayerComponent& component) {

	if (auto* current = world.TryGetComponent<AnimationClipPlayerComponent>(entity)) {
		ApplyAnimationClipPlayerSettings(component, *current);
		world.MarkComponentModified<AnimationClipPlayerComponent>(entity);
	}
}

void Engine::AnimationClipPlayerInspectorDrawer::DrawFields(const EditorPanelContext& context, ECSWorld& world,
	const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&] { if (draft.unscaledTime) draft.timeSource.mode = TimeSourceMode::Unscaled; auto result = DrawTimeSourceField(context, world, entity, draft.timeSource); if (result.valueChanged) draft.unscaledTime = false; return result; });
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField(Engine::GetInspectorLabel(Engine::InspectorFieldID::PlayOnStart), draft.playOnStart); });
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField(Engine::GetInspectorLabel(Engine::InspectorFieldID::PlayInEditMode), draft.playInEditMode); });
	DrawField(anyItemActive, [&] { return MyGUI::AssetReferenceField("クリップ", draft.clip,
		context.editorContext->assetDatabase, {AssetType::AnimationClip}); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat(Engine::GetInspectorLabel(Engine::InspectorFieldID::PlaybackSpeed), draft.speed, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f}); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat(GetInspectorLabel(InspectorFieldID::StartDelay), draft.startDelay, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1000000.0f}); });
	DrawField(anyItemActive, [&] { return MyGUI::EnumCombo(GetInspectorLabel(InspectorFieldID::PlaybackMode), draft.wrapMode); });
	DrawField(anyItemActive, [&] { return MyGUI::DragInt("回数・0は無制限", draft.repeatCount, {.minValue = 0}); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("反復間隔", draft.interval, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1000000.0f}); });
	auto& current = world.GetComponent<AnimationClipPlayerComponent>(entity);
	const bool playbackEnabled = context.IsPlaying() || draft.playInEditMode;
	const auto action = DrawInspectorPlaybackControls(playbackEnabled, false);
	if (action != InspectorPlaybackAction::None) current.runtimeRequest = static_cast<int>(action);
	ImGui::BeginDisabled(!playbackEnabled);
	float time = current.runtimeTime;
	if (MyGUI::DragFloat("再生時刻", time, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = current.runtimeDuration}).valueChanged) {
		current.runtimeSeekTime = time;
		current.runtimeRequest = 6;
	}
	ImGui::EndDisabled();
	MyGUI::Text(GetInspectorLabel(InspectorFieldID::PlaybackState), GetInspectorPlaybackState(current.runtimePlaying, current.runtimePaused, current.runtimeFinished));
	if (!current.runtimeDiagnostic.empty()) MyGUI::Text("適用状態", current.runtimeDiagnostic);
}
