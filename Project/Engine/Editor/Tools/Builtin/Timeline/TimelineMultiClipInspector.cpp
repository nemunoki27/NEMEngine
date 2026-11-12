#include "TimelineMultiClipInspector.h"
#include "TimelineEditorFields.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
// imgui
#include <imgui_internal.h>

using namespace Engine;

namespace {

	TimelineClip* FindClip(TimelineAsset& asset, const TimelineClipSelection& selection) {

		for (auto& track : asset.tracks) if (track.id == selection.track && !track.locked)
			for (auto& clip : track.clips) if (clip.id == selection.clip) return &clip;
		return nullptr;
	}

	bool HasSourceTime(TimelineTrackKind kind) {

		return kind == TimelineTrackKind::SkinnedAnimation || kind == TimelineTrackKind::Motion ||
			kind == TimelineTrackKind::Particle || kind == TimelineTrackKind::Camera || kind == TimelineTrackKind::Audio ||
			kind == TimelineTrackKind::Property || kind == TimelineTrackKind::SubTimeline || kind == TimelineTrackKind::Custom;
	}
}

ValueEditResult Engine::DrawTimelineMultiClipInspector(TimelineAsset& asset,
	std::span<const TimelineClipSelection> selection, TimelineEditMode mode, bool autoBlend, bool frames) {

	ValueEditResult result;
	MyGUI::TextUInt("選択クリップ数", static_cast<uint32_t>(selection.size()));
	std::vector<TimelineClipSelection> editable;
	for (const auto& item : selection) if (FindClip(asset, item)) editable.push_back(item);
	if (editable.empty()) return result;
	const auto first = *FindClip(asset, editable.front());
	const bool mixedEnabled = std::any_of(editable.begin(), editable.end(), [&](const auto& item) {
		return FindClip(asset, item)->enabled != first.enabled;
	});
	bool enabled = first.enabled;
	ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixedEnabled);
	const auto enabledEdit = MyGUI::CheckboxField("有効", enabled);
	ImGui::PopItemFlag();
	result.Merge(enabledEdit);
	if (enabledEdit.valueChanged) for (const auto& item : editable) FindClip(asset, item)->enabled = enabled;
	double start = first.start;
	const auto startEdit = TimelineEditorFields::Time("開始", start, asset.frameRate, frames);
	result.Merge(startEdit);
	if (startEdit.valueChanged) {
		const auto before = asset;
		TimelineEditOperations::Apply(asset, before, editable, editable.front().track, editable.front().clip,
			TimelineClipEditKind::Move, start - first.start, mode, autoBlend);
		return result;
	}
	double duration = first.duration;
	const auto durationEdit = TimelineEditorFields::Time("長さ", duration, asset.frameRate, frames, 1.0 / asset.frameRate);
	result.Merge(durationEdit);
	if (durationEdit.valueChanged) {
		for (const auto& item : editable) {
			const auto* clip = FindClip(asset, item);
			if (!clip) continue;
			const double delta = duration - clip->duration;
			const auto before = asset;
			TimelineEditOperations::Apply(asset, before, {&item, 1}, item.track, item.clip,
				TimelineClipEditKind::End, delta, mode, autoBlend);
		}
		return result;
	}
	const bool sourceTime = std::all_of(editable.begin(), editable.end(), [&](const auto& item) {
		const auto track = std::find_if(asset.tracks.begin(), asset.tracks.end(), [&](const auto& value) { return value.id == item.track; });
		return track != asset.tracks.end() && HasSourceTime(track->kind);
	});
	if (sourceTime) {
		double clipIn = first.clipIn;
		const auto clipInEdit = TimelineEditorFields::Time("素材の開始位置", clipIn, asset.frameRate, frames);
		result.Merge(clipInEdit);
		if (clipInEdit.valueChanged) for (const auto& item : editable) FindClip(asset, item)->clipIn = clipIn;
		float speed = static_cast<float>(first.speed);
		const auto speedEdit = MyGUI::DragFloat("素材の再生速度", speed, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f});
		result.Merge(speedEdit);
		if (speedEdit.valueChanged) for (const auto& item : editable) FindClip(asset, item)->speed = speed;
	}
	return result;
}
