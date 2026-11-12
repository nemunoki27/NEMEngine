#include "TimelineAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

// c++
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace Engine;

TimelineAsset Engine::CreateDefaultTimelineAsset(std::string name) {

	TimelineAsset asset;
	asset.name = std::move(name);
	asset.durationMode = TimelineDurationMode::BasedOnClips;
	asset.bindings.push_back({UUID::New(), "Actor", {}});
	UpdateTimelineAssetDuration(asset);
	return asset;
}

double Engine::GetTimelineClipSourceDuration(const TimelineClip& clip) {

	return clip.sourceDuration > 0.0 ? clip.sourceDuration : clip.duration;
}

double Engine::GetTimelineAssetDuration(const TimelineAsset& asset) {

	if (asset.durationMode != TimelineDurationMode::BasedOnClips) return asset.duration;
	double duration = 1.0 / (std::max)(asset.frameRate, 1.0);
	for (const auto& track : asset.tracks) for (const auto& clip : track.clips)
		duration = (std::max)(duration, clip.start + clip.duration);
	return duration;
}

void Engine::UpdateTimelineAssetDuration(TimelineAsset& asset) {

	asset.duration = GetTimelineAssetDuration(asset);
}

bool Engine::ValidateTimelineAsset(const TimelineAsset& asset, std::string& diagnostic) {

	auto fail = [&](std::string text) { diagnostic = std::move(text); return false; };
	auto validTime = [](double time) { return std::isfinite(time) && time >= 0.0; };
	if (asset.schemaVersion != 1) return fail("未対応のTimeline形式です");
	if (asset.durationMode != TimelineDurationMode::FixedLength && asset.durationMode != TimelineDurationMode::BasedOnClips)
		return fail("再生時間の設定が不正です");
	if (!validTime(asset.duration) || asset.duration <= 0.0 || !std::isfinite(asset.frameRate) || asset.frameRate <= 0.0)
		return fail("再生時間または基準FPSが不正です");
	if (asset.tracks.size() > 4096 || asset.bindings.size() > 4096 || asset.parameters.size() > 4096)
		return fail("Timelineの設定数が上限を超えています");
	std::unordered_set<UUID> identifiers, bindings, parameters;
	std::unordered_set<std::string> bindingNames, parameterNames;
	for (const auto& binding : asset.bindings) {
		if (!binding.id || binding.name.empty() || !identifiers.insert(binding.id).second ||
			!bindingNames.insert(binding.name).second) return fail("割り当ての識別子または名前が重複しています");
		bindings.insert(binding.id);
	}
	for (const auto& parameter : asset.parameters) {
		if (!parameter.id || parameter.name.empty() || !identifiers.insert(parameter.id).second ||
			!parameterNames.insert(parameter.name).second) return fail("パラメーターの識別子または名前が重複しています");
		parameters.insert(parameter.id);
	}
	std::unordered_map<UUID, const TimelineTrack*> tracks;
	size_t clipCount = 0;
	for (const auto& track : asset.tracks) {
		if (!track.id || !identifiers.insert(track.id).second) return fail("トラックの識別子が重複しています");
		if (GetTimelineTrackKindName(track.kind).empty()) return fail("トラックの種類が不正です");
		if (track.binding && !bindings.contains(track.binding)) return fail("トラックの割り当て先が見つかりません");
		if (!validTime(track.start) || !validTime(track.duration)) return fail("グループの時間が不正です");
		tracks.emplace(track.id, &track);
		clipCount += track.clips.size();
		if (clipCount > 131072) return fail("クリップ数が上限を超えています");
		for (const auto& clip : track.clips) {
			if (!clip.id || !identifiers.insert(clip.id).second) return fail("クリップの識別子が重複しています");
			if (!validTime(clip.start) || !validTime(clip.duration) || !validTime(clip.clipIn) ||
				!validTime(clip.speed) || !validTime(clip.sourceDuration) || !validTime(clip.blendIn) || !validTime(clip.blendOut) || !clip.data.is_object())
				return fail("クリップの時間または設定が不正です");
			if (!std::isfinite(clip.start + clip.duration) || !std::isfinite(clip.clipIn + clip.duration * clip.speed))
				return fail("クリップの終端時刻が範囲外です");
		}
		for (const auto& condition : track.conditions) {
			if (static_cast<unsigned>(condition.kind) > 2 || static_cast<unsigned>(condition.comparison) > 5 ||
				static_cast<unsigned>(condition.plane) > 2 || !std::isfinite(condition.distance) || condition.distance < 0)
				return fail("分岐条件が不正です");
			if (condition.kind == TimelineConditionKind::Parameter && !parameters.contains(condition.parameter))
				return fail("条件のパラメーターが見つかりません");
			if (condition.kind != TimelineConditionKind::Parameter && !bindings.contains(condition.firstBinding))
				return fail("条件の割り当て先が見つかりません");
			if (condition.kind == TimelineConditionKind::Distance && !bindings.contains(condition.secondBinding))
				return fail("距離を測る対象が見つかりません");
		}
	}
	for (const auto& track : asset.tracks) {
		std::unordered_set<UUID> visited{track.id};
		UUID parent = track.parent;
		while (parent) {
			const auto found = tracks.find(parent);
			if (found == tracks.end() || found->second->kind != TimelineTrackKind::Group)
				return fail("親グループが見つかりません");
			if (!visited.insert(parent).second) return fail("グループの親子関係が循環しています");
			parent = found->second->parent;
		}
	}
	diagnostic.clear();
	return true;
}

bool Engine::LoadTimelineAsset(const std::filesystem::path& path, TimelineAsset& asset, std::string* diagnostic) {

	try {
		nlohmann::json json;
		if (!JsonFile::TryLoad(path, json)) {
			if (diagnostic) *diagnostic = "Timelineアセットを読み込めません";
			return false;
		}
		auto candidate = json.get<TimelineAsset>();
		asset = std::move(candidate);
		if (diagnostic) diagnostic->clear();
		return true;
	} catch (const std::exception& error) {
		if (diagnostic) *diagnostic = error.what();
		return false;
	}
}

bool Engine::SaveTimelineAsset(const std::filesystem::path& path, const TimelineAsset& asset, std::string* diagnostic) {

	std::string message;
	if (!ValidateTimelineAsset(asset, message)) {
		if (diagnostic) *diagnostic = std::move(message);
		return false;
	}
	try {
		if (!JsonFile::Save(path, nlohmann::json(asset))) {
			if (diagnostic) *diagnostic = "Timelineアセットを保存できません";
			return false;
		}
		if (diagnostic) diagnostic->clear();
		return true;
	} catch (const std::exception& error) {
		if (diagnostic) *diagnostic = error.what();
		return false;
	}
}
