#include "TimelineEditOperations.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>
#include <iterator>

using namespace Engine;

namespace {

	bool Selected(std::span<const TimelineClipSelection> selection, UUID track, UUID clip) {

		return std::any_of(selection.begin(), selection.end(), [&](const auto& value) {
			return value.track == track && value.clip == clip;
		});
	}

	// トリミング前の素材長を確定する
	void PreserveSource(TimelineClip& clip) {

		clip.sourceDuration = GetTimelineClipSourceDuration(clip);
	}

	// 指定区間を残し、素材の開始位置を追従させる
	TimelineClip Slice(const TimelineClip& source, double start, double end, bool newIdentity) {

		auto result = source;
		PreserveSource(result);
		if (newIdentity) result.id = UUID::New();
		result.clipIn += (start - source.start) * source.speed;
		result.start = start;
		result.duration = end - start;
		result.blendIn = start > source.start ? 0.0 : (std::min)(source.blendIn, result.duration);
		result.blendOut = end < source.start + source.duration ? 0.0 : (std::min)(source.blendOut, result.duration);
		return result;
	}

	// 選択範囲と重なる素材を切り分ける
	bool ReplaceOverlaps(TimelineTrack& track, std::span<const TimelineClipSelection> selection) {

		std::vector<std::pair<double, double>> ranges;
		for (const auto& clip : track.clips) if (Selected(selection, track.id, clip.id)) {
			if (!TimelineEditOperations::CanSplit(track.kind, clip)) return false;
			ranges.emplace_back(clip.start, clip.start + clip.duration);
		}
		std::sort(ranges.begin(), ranges.end());
		std::vector<TimelineClip> result;
		for (const auto& clip : track.clips) {
			if (Selected(selection, track.id, clip.id)) { result.push_back(clip); continue; }
			std::vector<TimelineClip> pieces{clip};
			for (const auto& [start, end] : ranges) {
				std::vector<TimelineClip> next;
				for (const auto& piece : pieces) {
					const double finish = piece.start + piece.duration;
					if (piece.start >= end || finish <= start) { next.push_back(piece); continue; }
					if (!TimelineEditOperations::CanSplit(track.kind, piece)) return false;
					const bool left = piece.start < start;
					if (left) next.push_back(Slice(piece, piece.start, start, false));
					if (finish > end) next.push_back(Slice(piece, end, finish, left));
				}
				pieces = std::move(next);
			}
			result.insert(result.end(), pieces.begin(), pieces.end());
		}
		track.clips = std::move(result);
		return true;
	}

	void RebuildBlends(TimelineTrack& track) {

		for (auto& clip : track.clips) { clip.blendIn = 0; clip.blendOut = 0; }
		for (auto& first : track.clips) for (auto& second : track.clips) {
			if (first.id == second.id || first.start >= second.start) continue;
			const double overlap = (std::min)({first.start + first.duration - second.start, first.duration, second.duration});
			if (overlap <= 0.0) continue;
			first.blendOut = (std::max)(first.blendOut, overlap);
			second.blendIn = (std::max)(second.blendIn, overlap);
		}
	}
}

bool TimelineEditOperations::CanSplit(TimelineTrackKind kind, const TimelineClip& clip) {

	if (kind == TimelineTrackKind::SkinnedAnimation || kind == TimelineTrackKind::Audio) return true;
	const auto source = clip.data.find("asset");
	return kind == TimelineTrackKind::Property && source != clip.data.end() && source->is_string() &&
		!source->get_ref<const std::string&>().empty();
}

bool TimelineEditOperations::SupportsBlend(TimelineTrackKind kind) {

	return kind == TimelineTrackKind::SkinnedAnimation || kind == TimelineTrackKind::Property ||
		kind == TimelineTrackKind::Camera || kind == TimelineTrackKind::Audio;
}

void TimelineEditOperations::UpdateDuration(TimelineAsset& asset) {

	UpdateTimelineAssetDuration(asset);
}

bool TimelineEditOperations::Apply(TimelineAsset& asset, const TimelineAsset& before,
	std::span<const TimelineClipSelection> selection, UUID anchorTrack, UUID anchorClip,
	TimelineClipEditKind kind, double delta, TimelineEditMode mode, bool autoBlend) {

	if (selection.empty() || !std::isfinite(delta)) return false;
	TimelineAsset candidate = before;
	const double frame = 1.0 / before.frameRate;
	if (kind == TimelineClipEditKind::Move) for (const auto& track : before.tracks) {
		if (track.locked) continue;
		for (const auto& clip : track.clips) if (Selected(selection, track.id, clip.id))
			delta = (std::max)(delta, -clip.start);
	}
	bool changed = false;
	for (auto& track : candidate.tracks) {
		if (track.locked) continue;
		double oldEnd = -1.0, newEnd = -1.0;
		for (auto& clip : track.clips) {
			if (!Selected(selection, track.id, clip.id) ||
				(kind != TimelineClipEditKind::Move && (track.id != anchorTrack || clip.id != anchorClip))) continue;
			const auto original = clip;
			if (kind != TimelineClipEditKind::Move) PreserveSource(clip);
			oldEnd = (std::max)(oldEnd, original.start + original.duration);
			if (kind == TimelineClipEditKind::Move) clip.start = original.start + delta;
			else if (kind == TimelineClipEditKind::End) clip.duration = (std::max)(frame, original.duration + delta);
			else {
				const double minimum = original.speed > 0.0 ? -original.clipIn / original.speed : -original.start;
				const double offset = std::clamp(delta, (std::max)(-original.start, minimum),
					original.duration - (std::min)(frame, original.duration));
				clip.start += offset;
				clip.duration -= offset;
				clip.clipIn = (std::max)(0.0, original.clipIn + offset * original.speed);
			}
			clip.blendIn = (std::min)(clip.blendIn, clip.duration);
			clip.blendOut = (std::min)(clip.blendOut, clip.duration);
			newEnd = (std::max)(newEnd, clip.start + clip.duration);
			changed |= clip.start != original.start || clip.duration != original.duration;
		}
		if (oldEnd < 0.0) continue;
		if (mode == TimelineEditMode::Ripple) for (auto& clip : track.clips) {
			if (!Selected(selection, track.id, clip.id) && clip.start >= oldEnd) clip.start += newEnd - oldEnd;
		}
		if (mode == TimelineEditMode::Replace && !ReplaceOverlaps(track, selection)) { asset = before; return false; }
		if (autoBlend && mode == TimelineEditMode::Mix && SupportsBlend(track.kind)) RebuildBlends(track);
	}
	if (!changed) { asset = before; return false; }
	UpdateDuration(candidate);
	asset = std::move(candidate);
	return true;
}

bool TimelineEditOperations::Split(TimelineAsset& asset, UUID trackID, UUID clipID, double time) {

	for (auto& track : asset.tracks) {
		if (track.id != trackID || track.locked) continue;
		for (auto& clip : track.clips) {
			if (clip.id != clipID || !CanSplit(track.kind, clip)) continue;
			const double end = clip.start + clip.duration;
			const double frame = 1.0 / asset.frameRate;
			if (time < clip.start + frame || time > end - frame) return false;
			const auto original = clip;
			clip = Slice(original, original.start, time, false);
			track.clips.push_back(Slice(original, time, end, true));
			return true;
		}
	}
	return false;
}

bool TimelineEditOperations::Insert(TimelineAsset& asset, UUID trackID, TimelineClip clip, TimelineEditMode mode, bool autoBlend) {

	auto candidate = asset;
	const auto track = std::find_if(candidate.tracks.begin(), candidate.tracks.end(), [&](const auto& value) { return value.id == trackID; });
	if (track == candidate.tracks.end() || track->locked || !(clip.duration > 0.0)) return false;
	if (mode == TimelineEditMode::Replace && !CanSplit(track->kind, clip)) return false;
	if (mode == TimelineEditMode::Ripple) for (auto& following : track->clips)
		if (following.start >= clip.start) following.start += clip.duration;
	const TimelineClipSelection inserted{trackID, clip.id};
	track->clips.push_back(std::move(clip));
	if (mode == TimelineEditMode::Replace && !ReplaceOverlaps(*track, {&inserted, 1})) return false;
	if (mode == TimelineEditMode::Mix && autoBlend && SupportsBlend(track->kind)) RebuildBlends(*track);
	UpdateDuration(candidate);
	asset = std::move(candidate);
	return true;
}

double TimelineEditOperations::SnapTime(const TimelineAsset& asset,
	std::span<const TimelineClipSelection> selection, double time, double playhead, double tolerance) {

	double result = time, distance = tolerance;
	const auto candidate = [&](double value) {
		const double difference = std::abs(value - time);
		if (difference < distance) { result = value; distance = difference; }
	};
	candidate(0.0); candidate(asset.duration); candidate(playhead);
	for (const auto& track : asset.tracks) for (const auto& clip : track.clips) {
		if (Selected(selection, track.id, clip.id)) continue;
		candidate(clip.start); candidate(clip.start + clip.duration);
	}
	return result;
}

bool TimelineEditOperations::MoveTrack(TimelineAsset& asset, UUID trackID, UUID destinationID, TimelineTrackDropPosition position) {

	if (trackID == destinationID) return false;
	auto source = std::find_if(asset.tracks.begin(), asset.tracks.end(), [&](const auto& value) { return value.id == trackID; });
	auto destination = std::find_if(asset.tracks.begin(), asset.tracks.end(), [&](const auto& value) { return value.id == destinationID; });
	if (source == asset.tracks.end() || destination == asset.tracks.end() || source->locked || destination->locked) return false;
	const bool intoGroup = position == TimelineTrackDropPosition::IntoGroup;
	if (intoGroup && destination->kind != TimelineTrackKind::Group) return false;
	const UUID parent = intoGroup ? destinationID : destination->parent;
	for (UUID ancestor = parent; ancestor;) {
		if (ancestor == trackID) return false;
		const auto found = std::find_if(asset.tracks.begin(), asset.tracks.end(), [&](const auto& value) { return value.id == ancestor; });
		if (found == asset.tracks.end()) return false;
		ancestor = found->parent;
	}
	auto moved = std::move(*source);
	moved.parent = parent;
	asset.tracks.erase(source);
	destination = std::find_if(asset.tracks.begin(), asset.tracks.end(), [&](const auto& value) { return value.id == destinationID; });
	asset.tracks.insert(position == TimelineTrackDropPosition::Before ? destination : std::next(destination), std::move(moved));
	return true;
}
