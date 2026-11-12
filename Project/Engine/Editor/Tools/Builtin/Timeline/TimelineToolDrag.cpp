#include "TimelineTool.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>

using namespace Engine;

void TimelineTool::UpdateDrag(const EditorToolContext& context) {

	if (dragMode_ == DragMode::None) return;
	const auto finish = [&] {
		dragMode_ = DragMode::None;
		snapGuide_ = -1.0;
		session_.CommitHistory();
	};
	if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		session_.GetAsset() = dragBefore_;
		session_.MarkChanged();
		finish();
		return;
	}
	const bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
	if (!mouseDown && !released) { finish(); return; }
	const auto& io = ImGui::GetIO();
	const float distance = io.MousePos.x - dragMouseX_;
	const float threshold = io.MouseDragThreshold;
	if (!ImGui::IsMousePosValid(&io.MousePos) ||
		(io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < threshold * threshold && std::abs(distance) < threshold)) {
		if (!mouseDown) finish();
		return;
	}
	const auto sourceTrack = std::find_if(dragBefore_.tracks.begin(), dragBefore_.tracks.end(),
		[&](const auto& value) { return value.id == draggedTrack_; });
	if (sourceTrack == dragBefore_.tracks.end()) { finish(); return; }
	const auto original = std::find_if(sourceTrack->clips.begin(), sourceTrack->clips.end(),
		[&](const auto& value) { return value.id == draggedClip_; });
	if (original == sourceTrack->clips.end()) { finish(); return; }
	double delta = distance / pixelsPerSecond_ + viewStart_ - dragViewStart_;
	snapGuide_ = -1.0;
	if (snap_ && !io.KeyShift) {
		const double frame = 1.0 / session_.GetAsset().frameRate;
		delta = std::round(delta / frame) * frame;
		const double edge = dragMode_ == DragMode::End ? original->start + original->duration : original->start;
		const double current = edge + delta;
		const double snapped = TimelineEditOperations::SnapTime(dragBefore_, selectedClips_, current, session_.time, 10.0 / pixelsPerSecond_);
		double adjustment = snapped - current;
		if (dragMode_ == DragMode::Move) {
			const double end = original->start + original->duration + delta;
			const double endSnap = TimelineEditOperations::SnapTime(dragBefore_, selectedClips_, end, session_.time, 10.0 / pixelsPerSecond_);
			if (endSnap != end && (adjustment == 0.0 || std::abs(endSnap - end) < std::abs(adjustment))) {
				adjustment = endSnap - end;
				snapGuide_ = endSnap;
			}
		}
		if (adjustment != 0.0 && snapGuide_ < 0.0) snapGuide_ = snapped;
		delta += adjustment;
	}
	if (delta != dragDelta_ || !dragApplied_) {
		const auto kind = dragMode_ == DragMode::Move ? TimelineClipEditKind::Move :
			dragMode_ == DragMode::Start ? TimelineClipEditKind::Start : TimelineClipEditKind::End;
		const bool changed = TimelineEditOperations::Apply(session_.GetAsset(), dragBefore_, selectedClips_,
			draggedTrack_, draggedClip_, kind, delta, editMode_, autoBlend_);
		if (changed || dragApplied_) session_.MarkChanged();
		dragApplied_ = true;
		dragDelta_ = delta;
	}
	if (!mouseDown) {
		if (dragMode_ == DragMode::Move && selectedClips_.size() == 1) {
			const float y = io.MousePos.y - canvasOrigin_.y;
			for (const auto row : visibleTracks_) {
				if (y < row.y || y >= row.y + row.height || row.id == draggedTrack_) continue;
				auto* destination = FindTrack(row.id);
				auto* source = FindTrack(draggedTrack_);
				if (!source || !destination || destination->locked || destination->kind != source->kind) break;
				auto clip = std::find_if(source->clips.begin(), source->clips.end(), [&](const auto& value) { return value.id == draggedClip_; });
				if (clip != source->clips.end()) {
					const UUID destinationID = destination->id;
					auto candidate = dragBefore_;
					for (auto& value : candidate.tracks) if (value.id == source->id) {
						std::erase_if(value.clips, [&](const auto& item) { return item.id == draggedClip_; });
						if (editMode_ == TimelineEditMode::Ripple) for (auto& item : value.clips)
							if (item.start >= original->start + original->duration) item.start -= original->duration;
					}
					if (TimelineEditOperations::Insert(candidate, destinationID, *clip, editMode_, autoBlend_)) {
						session_.GetAsset() = std::move(candidate);
						selectedTrack_ = destinationID;
						selectedClips_.front().track = destinationID;
						session_.MarkChanged();
						SelectInspection(context, destinationID, draggedClip_);
					}
				}
				break;
			}
		}
		finish();
	}
}
