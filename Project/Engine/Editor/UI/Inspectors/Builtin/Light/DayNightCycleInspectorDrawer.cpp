#include "DayNightCycleInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Inspectors/Common/TimeSourceFields.h>
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SkyAtmosphereComponent.h>

// c++
#include <format>

void Engine::DayNightCycleInspectorDrawer::ApplyPreview(ECSWorld& world, const Entity& entity,
	const DayNightCycleComponent& component) {

	if (auto* current = world.TryGetComponent<DayNightCycleComponent>(entity)) {
		ApplyDayNightSettings(component, *current);
		world.MarkComponentModified<DayNightCycleComponent>(entity);
	}
}

void Engine::DayNightCycleInspectorDrawer::DrawFields(const EditorPanelContext& context, ECSWorld& world,
	const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&] { return DrawTimeSourceField(context, world, entity, draft.timeSource); });
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField("編集中も時間を進める", draft.playInEditMode); });
	EntityEditSetting lightSetting;
	lightSetting.acceptEntity = [](ECSWorld& source, Entity target) {
		return source.TryGetComponentForBinding<DirectionalLightComponent>(target) != nullptr;
	};
	lightSetting.rejectReason = "DirectionalLightコンポーネントが必要です";
	DrawField(anyItemActive, [&] { return SceneEntityReferenceFields::Draw("太陽", world,
		entity, draft.sunEntity, lightSetting); });
	DrawField(anyItemActive, [&] { return SceneEntityReferenceFields::Draw("月", world, entity, draft.moonEntity, lightSetting); });
	EntityEditSetting skySetting;
	skySetting.acceptEntity = [](ECSWorld& source, Entity target) {
		return source.TryGetComponentForBinding<SkyAtmosphereComponent>(target) != nullptr;
	};
	skySetting.rejectReason = "SkyAtmosphereコンポーネントが必要です";
	DrawField(anyItemActive, [&] { return SceneEntityReferenceFields::Draw("空", world, entity, draft.atmosphereEntity, skySetting); });
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField("時間を固定", draft.fixedTime); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat(draft.fixedTime ? "固定時刻" : "開始時刻", draft.startHour,
		{.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 24.0f}); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("一日の長さ（秒）", draft.dayDurationSeconds,
		{.dragSpeed = 1.0f, .minValue = 0.01f, .maxValue = 8640000.0f}); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("軌道の方位（度）", draft.orbitAzimuth,
		{.dragSpeed = 0.1f, .minValue = -360.0f, .maxValue = 360.0f}); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("正午の高度（度）", draft.noonAltitude,
		{.dragSpeed = 0.1f, .minValue = 0.1f, .maxValue = 90.0f}); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("月の時刻差（時間）", draft.moonTimeOffset,
		{.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 24.0f}); });
	auto& current = world.GetComponent<DayNightCycleComponent>(entity);
	const auto action = DrawInspectorPlaybackControls(!draft.fixedTime && (context.IsPlaying() || draft.playInEditMode), true);
	if (action != InspectorPlaybackAction::None) current.runtimeRequest = static_cast<int32_t>(action);
	const int32_t seconds = static_cast<int32_t>(current.runtimeCurrentHour * 3600.0f) % 86400;
	MyGUI::Text("現在時刻", std::format("{:02}:{:02}:{:02}", seconds / 3600, (seconds / 60) % 60, seconds % 60));
	MyGUI::TextVector3("太陽の方向", current.runtimeSunDirection);
	MyGUI::TextVector3("月の方向", current.runtimeMoonDirection);
	MyGUI::Text(GetInspectorLabel(InspectorFieldID::PlaybackState), draft.fixedTime ? "時刻固定" :
		GetInspectorPlaybackState(current.runtimePlaying, current.runtimePaused, false, true));
	if (!current.runtimeDiagnostic.empty()) MyGUI::Text("昼夜制御", current.runtimeDiagnostic);
}
