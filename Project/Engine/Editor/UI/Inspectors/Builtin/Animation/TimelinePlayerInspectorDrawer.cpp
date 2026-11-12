#include "TimelinePlayerInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Editor/Tools/Builtin/Timeline/TimelineTool.h>
#include <Engine/Editor/Tools/Builtin/Timeline/TimelineEditorFields.h>
#include <Engine/Editor/UI/Inspectors/Common/TimelinePlayerFields.h>
#include <Engine/Editor/UI/Inspectors/Common/TimeSourceFields.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

using namespace Engine;

void TimelinePlayerInspectorDrawer::OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity,
	[[maybe_unused]] const TimelinePlayerComponent& component) {

	const auto* player = TryGetTimelinePlayer(world, entity);
	settings_ = player ? player->settings : TimelinePlayerSettings{};
}

void TimelinePlayerInspectorDrawer::SerializeDraft([[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const TimelinePlayerComponent& component, nlohmann::json& out) const {

	out = settings_;
}

void TimelinePlayerInspectorDrawer::ApplyPreview(ECSWorld& world, const Entity& entity,
	[[maybe_unused]] const TimelinePlayerComponent& component) {

	SetTimelinePlayerSettings(world, entity, settings_);
}

void TimelinePlayerInspectorDrawer::DrawFields(const EditorPanelContext& context, ECSWorld& world,
	const Entity& entity, bool& active) {

	DrawField(active, [&] { return MyGUI::CheckboxField(GetInspectorLabel(InspectorFieldID::Enabled), settings_.enabled); });
	DrawField(active, [&] {
		return DrawPlaybackStartFields(context, world, entity, settings_.timeSource, settings_.playInEditMode, &settings_.playOnStart);
	});
	DrawField(active, [&] { return MyGUI::AssetReferenceField("タイムライン", settings_.timeline,
		context.editorContext->assetDatabase, {AssetType::Timeline}); });
	DrawField(active, [&] { return MyGUI::DragFloat(GetInspectorLabel(InspectorFieldID::PlaybackSpeed), settings_.speed,
		{.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f}); });
	DrawField(active, [&] { return TimelineEditorFields::Time(GetInspectorLabel(InspectorFieldID::StartDelay),
		settings_.startDelay, 60.0, false); });
	DrawField(active, [&] { return DrawInspectorLoopField(settings_.loop); });
	DrawField(active, [&] { return MyGUI::DragInt("優先度", settings_.priority); });
	const auto* database = context.editorContext->assetDatabase;
	const uint64_t revision = database ? database->GetContentRevision(settings_.timeline) : 0;
	if (cachedAsset_ != settings_.timeline || cachedRevision_ != revision) {
		cachedAsset_ = settings_.timeline;
		cachedRevision_ = revision;
		assetError_.clear();
		assetLoaded_ = database && settings_.timeline && LoadTimelineAsset(database->ResolveFullPath(settings_.timeline), asset_, &assetError_) &&
			ValidateTimelineAsset(asset_, assetError_);
	}
	if (assetLoaded_) {
		DrawField(active, [&] { return DrawTimelineBindingFields(world, entity, settings_, asset_); });
		if (!asset_.parameters.empty() && MyGUI::CollapsingHeader("パラメーター上書き")) {
			ImGui::Indent();
			DrawField(active, [&] { return DrawTimelineParameterOverrides(settings_, asset_); });
			ImGui::Unindent();
		}
	}
	ImGui::BeginDisabled(!settings_.timeline);
	if (ImGui::Button("タイムラインを編集", ImVec2(-1, 0))) {
		if (auto* tool = dynamic_cast<TimelineTool*>(ToolRegistry::GetInstance().Find("engine.timeline")))
			tool->OpenPlayer(settings_.timeline, world.GetUUID(entity));
	}
	ImGui::EndDisabled();
	ImGui::Separator();
	if (auto* player = TryGetTimelinePlayer(world, entity)) {
		auto& runtime = player->runtime;
		const bool enabled = context.IsPlaying() || settings_.playInEditMode;
		const auto action = DrawInspectorPlaybackControls(enabled, false);
		if (action != InspectorPlaybackAction::None) runtime.request = static_cast<int32_t>(action);
		ImGui::BeginDisabled(!enabled);
		double time = runtime.time;
		if (TimelineEditorFields::Time("再生時刻", time, asset_.frameRate, false).valueChanged) {
			runtime.seekTime = time;
			runtime.request = 6;
		}
		ImGui::EndDisabled();
		MyGUI::Text(GetInspectorLabel(InspectorFieldID::PlaybackState),
			GetInspectorPlaybackState(runtime.playing, runtime.paused, runtime.finished));
		MyGUI::TextDouble(GetInspectorLabel(InspectorFieldID::Duration), runtime.duration);
		MyGUI::TextUInt64("周回数", runtime.cycles);
		if (!runtime.diagnostic.empty()) MyGUI::Text("適用状態", runtime.diagnostic);
	}
	if (!assetError_.empty()) MyGUI::Text("アセット", assetError_);
}
