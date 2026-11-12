#include "TimelineTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Editor/Core/EditorState.h>

// c++
#include <algorithm>
#include <limits>

using namespace Engine;

void TimelineTool::FollowSelection(const EditorToolContext& context) {

	if (targetLocked_ || pending_ || !context.panelContext || !context.panelContext->editorState) return;
	const auto& state = *context.panelContext->editorState;
	if (state.selectionKind == EditorSelectionKind::Asset &&
		(state.selectedAsset != lastFollowAsset_ || state.assetSelectionRevision != lastFollowAssetRevision_)) {
		lastFollowAsset_ = state.selectedAsset;
		lastFollowAssetRevision_ = state.assetSelectionRevision;
		const auto* meta = context.toolContext.assetDatabase ? context.toolContext.assetDatabase->Find(state.selectedAsset) : nullptr;
		if (meta && meta->type == AssetType::Timeline) OpenAsset(meta->guid);
	}
	if (state.selectionKind != EditorSelectionKind::Entity || !context.GetWorld() ||
		!context.GetWorld()->IsAlive(state.selectedEntity)) { lastFollowOwner_ = {}; return; }
	const UUID owner = context.GetWorld()->GetUUID(state.selectedEntity);
	if (lastFollowOwner_ == owner) return;
	lastFollowOwner_ = owner;
	if (const auto* player = TryGetTimelinePlayer(*context.GetWorld(), state.selectedEntity); player && player->settings.timeline) {
		if (player->settings.timeline != session_.GetAssetID() || owner != session_.GetOwnerID())
			OpenPlayer(player->settings.timeline, owner);
	}
}

void TimelineTool::ResetInteraction() {

	if (dragMode_ != DragMode::None) session_.CommitHistory();
	dragMode_ = DragMode::None;
	clipAction_ = ClipAction::None;
	boxSelecting_ = false;
	rangeDrag_ = 0;
	snapGuide_ = -1.0;
	draggedTrack_ = {}; draggedClip_ = {};
	movedTrack_ = {}; moveDestination_ = {};
	dragBefore_ = {};
	dragApplied_ = false;
}

void TimelineTool::SelectInspection(const EditorToolContext& context, UUID track, UUID clip) {

	if (!context.panelContext || !context.panelContext->editorState) return;
	context.panelContext->editorState->SelectInspectionTarget({descriptor_.id, session_.GetAssetID(), track, clip});
}

void TimelineTool::FrameSelection(bool all) {

	if (!session_.HasAsset()) return;
	double begin = (std::numeric_limits<double>::max)(), end = 0.0;
	for (const auto& track : session_.GetAsset().tracks) for (const auto& clip : track.clips) {
		if (!all && !IsSelected(track.id, clip.id)) continue;
		begin = (std::min)(begin, clip.start);
		end = (std::max)(end, clip.start + clip.duration);
	}
	if (begin > end) { begin = 0.0; end = session_.GetAsset().duration; }
	const double span = (std::max)(end - begin, 1.0 / session_.GetAsset().frameRate);
	viewStart_ = (std::max)(0.0, begin - span * 0.05);
	pixelsPerSecond_ = (std::clamp)(static_cast<float>((canvasWidth_ - labelWidth_) / (span * 1.1)), 8.0f, 3000.0f);
}

void TimelineTool::ApplyClipTiming(UUID trackID, UUID clipID, double start, double duration, double clipIn) {

	const TimelineAsset before = session_.GetAsset();
	const auto* clip = FindClip(trackID, clipID);
	if (!clip) return;
	const TimelineClipSelection selected{trackID, clipID};
	const auto apply = [&](TimelineClipEditKind kind, double delta) {
		if (TimelineEditOperations::Apply(session_.GetAsset(), before, {&selected, 1}, trackID, clipID,
			kind, delta, editMode_, autoBlend_)) session_.MarkChanged();
	};
	if (start != clip->start) apply(TimelineClipEditKind::Move, start - clip->start);
	else if (duration != clip->duration) apply(TimelineClipEditKind::End, duration - clip->duration);
	if (auto* next = FindClip(trackID, clipID); next && clipIn != next->clipIn) {
		next->clipIn = (std::max)(0.0, clipIn);
		session_.MarkChanged();
	}
}
