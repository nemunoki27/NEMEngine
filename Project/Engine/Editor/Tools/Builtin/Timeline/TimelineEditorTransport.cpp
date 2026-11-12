#include "TimelineEditorSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelinePlayerAccess.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/ECSWorldLifetime.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine;

void TimelineEditorSession::TogglePlayback(const EditorToolContext& context) {

	if (!hasAsset_) return;
	ECSWorld* world = context.GetWorld();
	if (!world || !world->IsAlive(GetOwner(context))) return;
	if (context.IsPlaying()) {
		if (auto* player = TryGetTimelinePlayer(*world, GetOwner(context))) {
			if (player->settings.timeline != assetID_) return;
			if (seekPending_ && seekWorld_.lock() == world->GetLifetime() && player->runtime.request == 6) {
				resumeAfterSeek_ = !resumeAfterSeek_;
				playing = resumeAfterSeek_;
				return;
			}
			resumeAfterSeek_ = false;
			scrubbing_ = false;
			player->runtime.request = player->runtime.playing ? (player->runtime.paused ? 3 : 2) : 1;
		}
		return;
	}
	scrubbing_ = false;
	previewEnabled = true;
	if (time >= asset_.duration || (rangeEnabled && (time < rangeStart || time >= rangeEnd)))
		time = rangeEnabled ? rangeStart : 0.0;
	playing = !playing;
}

void TimelineEditorSession::StopPlayback(const EditorToolContext& context) {

	scrubbing_ = false;
	seekPending_ = false;
	resumeAfterSeek_ = false;
	if (context.IsPlaying()) {
		ECSWorld* world = context.GetWorld();
		if (!world || !world->IsAlive(GetOwner(context))) return;
		if (auto* player = TryGetTimelinePlayer(*world, GetOwner(context));
			player && player->settings.timeline == assetID_) player->runtime.request = 4;
		return;
	}
	EndPreview();
	previewEnabled = false;
	time = 0.0;
}

void TimelineEditorSession::Seek(const EditorToolContext& context, double value) {

	if (!hasAsset_ || !std::isfinite(value)) return;
	value = (std::clamp)(value, 0.0, asset_.duration);
	resumeAfterSeek_ = false;
	if (context.IsPlaying()) {
		ECSWorld* world = context.GetWorld();
		if (!world || !world->IsAlive(GetOwner(context))) return;
		if (auto* player = TryGetTimelinePlayer(*world, GetOwner(context));
			player && player->settings.timeline == assetID_) {
			player->runtime.seekTime = value;
			player->runtime.paused = true;
			player->runtime.request = 6;
			seekPending_ = true;
			seekWorld_ = world->GetLifetime();
		}
	} else {
		playing = false;
		previewEnabled = true;
	}
	scrubbing_ = true;
	playing = false;
	time = value;
}

void TimelineEditorSession::Step(const EditorToolContext& context, int frames) {

	if (context.IsPlaying()) {
		ECSWorld* world = context.GetWorld();
		if (!world || !world->IsAlive(GetOwner(context))) return;
		if (auto* player = TryGetTimelinePlayer(*world, GetOwner(context));
			player && player->settings.timeline == assetID_) player->runtime.paused = true;
	}
	Seek(context, time + static_cast<double>(frames) / (std::max)(asset_.frameRate, 1.0));
}

void TimelineEditorSession::SyncPlayback(const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world) return;
	const Entity owner = GetOwner(context);
	if (world->IsAlive(owner)) ownerID_ = world->GetUUID(owner);
	if (!context.IsPlaying()) return;
	auto* player = TryGetTimelinePlayer(*world, owner);
	if (!player || player->settings.timeline != assetID_) {
		playing = false; seekPending_ = false; resumeAfterSeek_ = false; return;
	}
	if (seekWorld_.lock() != world->GetLifetime()) { seekPending_ = false; resumeAfterSeek_ = false; }
	if (seekPending_ && seekWorld_.lock() == world->GetLifetime() && player->runtime.request == 6) {
		playing = resumeAfterSeek_;
		return;
	}
	if (seekPending_ && resumeAfterSeek_) {
		player->runtime.request = player->runtime.playing ? 3 : 1;
		scrubbing_ = false;
	}
	seekPending_ = false;
	time = player->runtime.time;
	if (resumeAfterSeek_ && (player->runtime.request == 1 || player->runtime.request == 3)) { playing = true; return; }
	resumeAfterSeek_ = false;
	playing = player->runtime.playing && !player->runtime.paused;
}

void TimelineEditorSession::MarkChanged() {

	++draftRevision_;
	dirty_ = true;
}

void TimelineEditorSession::PublishDraft(const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!hasAsset_ || !world || sourceMissing_) return;
	const auto previous = publishedWorld_.lock();
	const bool sameWorld = previous && previous == world->GetLifetime();
	if (sameWorld && publishedRevision_ == draftRevision_) return;
	std::string diagnostic;
	if (!PublishTimelineAssetOverride(*world, assetID_, std::make_shared<TimelineAsset>(asset_), &diagnostic)) {
		draftError_ = std::move(diagnostic);
		error_ = draftError_;
		return;
	}
	if (error_ == draftError_) error_.clear();
	draftError_.clear();
	publishedWorld_ = world->GetLifetime();
	publishedRevision_ = draftRevision_;
}
