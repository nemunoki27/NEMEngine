#include "AnimationClipPlayerInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
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
	DrawField(anyItemActive, [&] { return MyGUI::AssetReferenceField("クリップ", draft.clip,
		context.editorContext->assetDatabase, {AssetType::AnimationClip}); });
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField(Engine::GetInspectorLabel(Engine::InspectorFieldID::PlayOnStart), draft.playOnStart); });
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField(Engine::GetInspectorLabel(Engine::InspectorFieldID::PlayInEditMode), draft.playInEditMode); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat(Engine::GetInspectorLabel(Engine::InspectorFieldID::PlaybackSpeed), draft.speed, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f}); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("開始待ち", draft.startDelay, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1000000.0f}); });
	DrawField(anyItemActive, [&] { return MyGUI::EnumCombo("再生方法", draft.wrapMode); });
	DrawField(anyItemActive, [&] { return MyGUI::DragInt("回数・0は無制限", draft.repeatCount, {.minValue = 0}); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("反復間隔", draft.interval, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1000000.0f}); });
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField(Engine::GetInspectorLabel(Engine::InspectorFieldID::UnscaledTime), draft.unscaledTime); });
	auto& current = world.GetComponent<AnimationClipPlayerComponent>(entity);
	const char* controls[]{"再生", "一時停止", "再開", "停止", "リセット"};
	const float width = MyGUI::CalcEqualItemWidth(5);
	for (int i = 0; i < 5; ++i) {
		if (i) ImGui::SameLine();
		if (ImGui::Button(controls[i], ImVec2(width, 0))) current.runtimeRequest = i + 1;
	}
	float time = current.runtimeTime;
	if (MyGUI::DragFloat("再生時刻", time, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = current.runtimeDuration}).valueChanged) {
		current.runtimeSeekTime = time;
		current.runtimeRequest = 6;
	}
	MyGUI::Text("再生状態", current.runtimePaused ? "一時停止" : current.runtimePlaying ? "再生中" : current.runtimeFinished ? "完了" : "停止中");
	if (!current.runtimeDiagnostic.empty()) MyGUI::Text("適用状態", current.runtimeDiagnostic);
}
