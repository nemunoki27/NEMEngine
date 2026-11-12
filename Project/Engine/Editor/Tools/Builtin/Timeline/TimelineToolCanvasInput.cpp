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

void TimelineTool::DrawCanvasInput(const EditorToolContext& context, ImVec2 origin, float width, float height) {

	const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	const auto& io = ImGui::GetIO();
	const float timeX = origin.x + labelWidth_;
	if (hovered && io.MouseWheel != 0.0f) {
		if (io.KeyCtrl) rowHeight_ = (std::clamp)(rowHeight_ + io.MouseWheel * 4.0f, 32.0f, 80.0f);
		else if (io.KeyShift) viewStart_ = (std::max)(0.0, viewStart_ - io.MouseWheel * 100.0 / pixelsPerSecond_);
		else if (io.MousePos.x >= timeX) {
			const double underMouse = viewStart_ + (io.MousePos.x - timeX) / pixelsPerSecond_;
			pixelsPerSecond_ = (std::clamp)(pixelsPerSecond_ * std::pow(1.2f, io.MouseWheel), 8.0f, 3000.0f);
			viewStart_ = (std::max)(0.0, underMouse - (io.MousePos.x - timeX) / pixelsPerSecond_);
		} else ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * rowHeight_ * 2.0f);
	}
	if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
		viewStart_ = (std::max)(0.0, viewStart_ - io.MouseDelta.x / pixelsPerSecond_);
		ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
		followPlayhead_ = false;
	}
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && io.MousePos.x >= timeX) {
		boxSelecting_ = true;
		boxStart_ = io.MousePos;
		if (!io.KeyCtrl) selectedClips_.clear();
	}
	if (boxSelecting_) {
		boxCurrent_ = io.MousePos;
		const ImVec2 lo((std::min)(boxStart_.x, boxCurrent_.x), (std::min)(boxStart_.y, boxCurrent_.y));
		const ImVec2 hi((std::max)(boxStart_.x, boxCurrent_.x), (std::max)(boxStart_.y, boxCurrent_.y));
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(ImVec2(timeX, origin.y), ImVec2(origin.x + width, origin.y + height), true);
		draw->AddRectFilled(lo, hi, GetColor(Color::SelectionFill));
		draw->AddRect(lo, hi, GetColor(Color::Accent));
		draw->PopClipRect();
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			for (const auto& [selection, rect] : clipRects_) if (rect.x <= hi.x && rect.z >= lo.x && rect.y <= hi.y && rect.w >= lo.y) {
				if (!IsSelected(selection.track, selection.clip)) selectedClips_.push_back(selection);
				selectedTrack_ = selection.track;
			}
			boxSelecting_ = false;
			if (selectedClips_.empty()) SelectInspection(context, selectedTrack_);
			else SelectInspection(context, selectedClips_.back().track, selectedClips_.back().clip);
		}
	}
	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered())
		ImGui::OpenPopup("TimelineCanvasMenu");
	if (ImGui::BeginPopup("TimelineCanvasMenu")) {
		if (ImGui::BeginMenu("トラックを追加")) { DrawAddTrackMenu(context); ImGui::EndMenu(); }
		if (auto* track = FindTrack(selectedTrack_); track && !track->locked && track->kind != TimelineTrackKind::Group) {
			const bool canAdd = editMode_ != TimelineEditMode::Replace || TimelineEditOperations::CanSplit(track->kind, TimelineClip{});
			if (ImGui::MenuItem("再生位置へクリップを追加", nullptr, false, canAdd) && AddClip(*track) && !selectedClips_.empty())
				SelectInspection(context, selectedTrack_, selectedClips_.front().clip);
			if (!canAdd && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("この種類の新規クリップはMixかRippleで追加してください");
		}
		if (ImGui::MenuItem("貼り付け", "Ctrl+V", false, !clipboard_.empty())) PasteSelection();
		ImGui::Separator();
		if (ImGui::MenuItem("全体を表示", "A")) FrameSelection(true);
		if (ImGui::MenuItem("選択範囲を表示", "F")) FrameSelection(false);
		ImGui::EndPopup();
	}
}
