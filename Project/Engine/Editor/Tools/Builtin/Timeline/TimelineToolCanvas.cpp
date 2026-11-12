#include "TimelineTool.h"
#include "TimelineAppearance.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>

using namespace Engine;
using TimelineAppearance::Color;
using TimelineAppearance::GetColor;

void TimelineTool::DrawCanvas(const EditorToolContext& context) {

	canvasWidth_ = ImGui::GetContentRegionAvail().x;
	const float minimumLabel = (std::min)(180.0f, (std::max)(96.0f, canvasWidth_ * 0.5f));
	labelWidth_ = std::clamp(labelWidth_, minimumLabel, (std::max)(minimumLabel, canvasWidth_ * 0.7f));
	const float scale = ImGui::GetFontSize() / 15.0f;
	const float rulerHeight = 32.0f * scale;
	const double span = (std::max)(1.0f, canvasWidth_ - labelWidth_) / pixelsPerSecond_;
	if (followPlayhead_ && session_.playing && (session_.time < viewStart_ || session_.time > viewStart_ + span))
		viewStart_ = (std::max)(0.0, session_.time - span * 0.1);
	DrawRuler(context, rulerHeight);
	RebuildVisibleTracks();
	const float barHeight = ImGui::GetFontSize() + 6.0f;
	const float bodyHeight = (std::max)(40.0f, ImGui::GetContentRegionAvail().y - barHeight);
	ImGui::BeginChild("TimelineRows", ImVec2(0, bodyHeight), ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollWithMouse);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	canvasOrigin_ = origin;
	const float width = ImGui::GetContentRegionAvail().x;
	const float timeX = origin.x + labelWidth_;
	const float lastEnd = visibleTracks_.empty() ? 0.0f : visibleTracks_.back().y + visibleTracks_.back().height;
	const float height = (std::max)(lastEnd + rowHeight_ * scale, ImGui::GetContentRegionAvail().y);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), GetColor(Color::Background));
	draw->PushClipRect(ImVec2(timeX, origin.y), ImVec2(origin.x + width, origin.y + height), true);
	const double step = GetTickStep();
	const double endTime = viewStart_ + (width - labelWidth_) / pixelsPerSecond_;
	for (double time = std::ceil(viewStart_ / step) * step; time <= endTime; time += step) {
		const float x = timeX + static_cast<float>((time - viewStart_) * pixelsPerSecond_);
		draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), GetColor(Color::Grid));
	}
	draw->PopClipRect();
	clipRects_.clear();
	for (const auto visible : visibleTracks_) {
		if (auto* track = FindTrack(visible.id)) {
			const ImVec2 position(origin.x, origin.y + visible.y);
			DrawTrackRow(context, *track, visible.depth, position, width, timeX, rowHeight_ * scale);
			if (expandedCurves_.contains(visible.id)) {
				ImGui::SetCursorScreenPos(ImVec2(timeX + 4, position.y + rowHeight_ * scale + 2));
				DrawTrackCurves(context, visible.id, ImVec2((std::max)(1.0f, width - labelWidth_ - 8), visible.height - rowHeight_ * scale - 6));
			}
		}
	}
	for (const auto& visible : visibleTracks_) {
		const float bottom = origin.y + visible.y + visible.height - 0.5f;
		draw->AddLine(ImVec2(origin.x, bottom), ImVec2(timeX, bottom), GetColor(Color::Divider));
	}
	if (clipAction_ == ClipAction::Delete) DeleteSelection();
	else if (clipAction_ == ClipAction::Duplicate) DuplicateSelection();
	else if (clipAction_ == ClipAction::Split) {
		for (const auto selection : selectedClips_) if (TimelineEditOperations::Split(session_.GetAsset(),
			selection.track, selection.clip, session_.time)) session_.MarkChanged();
	}
	clipAction_ = ClipAction::None;
	if (movedTrack_ && moveDestination_) {
		if (TimelineEditOperations::MoveTrack(session_.GetAsset(), movedTrack_, moveDestination_, movePosition_)) session_.MarkChanged();
		movedTrack_ = {}; moveDestination_ = {};
	}
	draw->PushClipRect(ImVec2(timeX, origin.y), ImVec2(origin.x + width, origin.y + height), true);
	const float durationX = timeX + static_cast<float>((session_.GetAsset().duration - viewStart_) * pixelsPerSecond_);
	if (durationX < origin.x + width) draw->AddRectFilled(ImVec2((std::max)(durationX, timeX), origin.y),
		ImVec2(origin.x + width, origin.y + height), IM_COL32(0, 0, 0, 85));
	const float playhead = timeX + static_cast<float>((session_.time - viewStart_) * pixelsPerSecond_);
	draw->AddLine(ImVec2(playhead, origin.y), ImVec2(playhead, origin.y + height), GetColor(Color::Playhead));
	if (snapGuide_ >= 0.0 && dragMode_ != DragMode::None) {
		const float x = timeX + static_cast<float>((snapGuide_ - viewStart_) * pixelsPerSecond_);
		draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), GetColor(Color::Accent));
	}
	draw->PopClipRect();
	UpdateDrag(context);
	DrawCanvasInput(context, origin, width, height);
	DrawAssetDrop(context, origin, width, height);
	ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
	ImGui::Dummy(ImVec2(width, 1));
	ImGui::EndChild();
	DrawZoomBar();
	DrawBindingChoice(context);
}
