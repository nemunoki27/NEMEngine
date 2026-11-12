#include "TimelineTool.h"
#include "TimelineAppearance.h"
#include "TimelineEditorFields.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>

using namespace Engine;
using TimelineAppearance::Color;
using TimelineAppearance::GetColor;

void TimelineTool::DrawClip(const EditorToolContext& context, TimelineTrack& track, TimelineClip& clip,
	ImVec2 origin, float timeX, float laneWidth, float rowHeight) {

	const bool marker = track.kind == TimelineTrackKind::Event;
	const float left = timeX + static_cast<float>((clip.start - viewStart_) * pixelsPerSecond_);
	const float right = marker ? left + 12.0f : left + (std::max)(8.0f, static_cast<float>(clip.duration * pixelsPerSecond_));
	if (right < timeX || left > timeX + laneWidth) return;
	const ImVec2 lo(marker ? left - 6 : left, origin.y + 4);
	const ImVec2 hi(right, origin.y + rowHeight - 4);
	clipRects_.push_back({{track.id, clip.id}, ImVec4(lo.x, lo.y, hi.x, hi.y)});
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImU32 accent = TimelineEditorFields::TrackColor(track.kind);
	if (track.muted || !clip.enabled) accent = (accent & 0x00ffffffu) | 0x70000000u;
	const bool selected = IsSelected(track.id, clip.id);
	if (marker) {
		const float y = origin.y + rowHeight * 0.5f;
		draw->AddQuadFilled(ImVec2(left, y - 7), ImVec2(left + 6, y), ImVec2(left, y + 7), ImVec2(left - 6, y), accent);
		if (selected) draw->AddQuad(ImVec2(left, y - 8), ImVec2(left + 7, y), ImVec2(left, y + 8), ImVec2(left - 7, y),
			GetColor(Color::Selection), 1.5f);
	} else {
		const bool active = session_.time >= clip.start && session_.time < clip.start + clip.duration;
		draw->AddRectFilled(lo, hi, selected ? GetColor(Color::SelectedClip) : active ? GetColor(Color::ActiveClip) : GetColor(Color::Clip));
		draw->AddRectFilled(ImVec2(lo.x, hi.y - 3), hi, accent);
		if (clip.blendIn > 0.0) {
			const float end = left + static_cast<float>((std::min)(clip.blendIn, clip.duration) * pixelsPerSecond_);
			draw->AddTriangleFilled(lo, ImVec2(end, lo.y), ImVec2(lo.x, hi.y), IM_COL32(0, 0, 0, 100));
			draw->AddLine(ImVec2(lo.x, hi.y), ImVec2(end, lo.y), GetColor(Color::Blend));
		}
		if (clip.blendOut > 0.0) {
			const float begin = right - static_cast<float>((std::min)(clip.blendOut, clip.duration) * pixelsPerSecond_);
			draw->AddTriangleFilled(ImVec2(begin, lo.y), ImVec2(hi.x, lo.y), hi, IM_COL32(0, 0, 0, 100));
			draw->AddLine(ImVec2(begin, lo.y), hi, GetColor(Color::Blend));
		}
		if (selected) draw->AddRect(lo, hi, GetColor(Color::Selection));
		const ImVec4 textClip((std::max)(left + 7, timeX + 2), lo.y, (std::min)(right - 5, timeX + laneWidth), hi.y);
		if (textClip.z - textClip.x > 5.0f) draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
			ImVec2(textClip.x, lo.y + (hi.y - lo.y - ImGui::GetFontSize()) * 0.5f - 1),
			GetColor(Color::Text), clip.name.c_str(), nullptr, 0.0f, &textClip);
	}
	ImGui::SetCursorScreenPos(ImVec2((std::max)(lo.x, timeX), lo.y));
	ImGui::PushID(ToString(clip.id).c_str());
	ImGui::InvisibleButton("Clip", ImVec2((std::max)(1.0f, (std::min)(right, timeX + laneWidth) - (std::max)(lo.x, timeX)), hi.y - lo.y));
	if (ImGui::IsItemClicked()) {
		selectedTrack_ = track.id;
		const bool alreadySelected = IsSelected(track.id, clip.id);
		if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !alreadySelected) selectedClips_.clear();
		if (ImGui::GetIO().KeyCtrl && alreadySelected) std::erase_if(selectedClips_,
			[&](const auto& value) { return value.track == track.id && value.clip == clip.id; });
		else if (!alreadySelected) selectedClips_.push_back({track.id, clip.id});
		SelectInspection(context, track.id, clip.id);
		if (!track.locked && IsSelected(track.id, clip.id) &&
			(editMode_ != TimelineEditMode::Replace || TimelineEditOperations::CanSplit(track.kind, clip))) {
			dragBefore_ = session_.GetAsset();
			const auto& io = ImGui::GetIO();
			dragMouseX_ = ImGui::IsMousePosValid(&io.MouseClickedPos[ImGuiMouseButton_Left]) ?
				io.MouseClickedPos[ImGuiMouseButton_Left].x : io.MousePos.x;
			dragViewStart_ = viewStart_;
			dragDelta_ = 0.0;
			dragApplied_ = false;
			draggedClip_ = clip.id;
			draggedTrack_ = track.id;
			dragMode_ = !marker && dragMouseX_ - left < 7 ? DragMode::Start :
				!marker && right - dragMouseX_ < 7 ? DragMode::End : DragMode::Move;
		}
	}
	if (ImGui::IsItemHovered()) {
		if (!track.locked && !marker && (ImGui::GetIO().MousePos.x - left < 7 || right - ImGui::GetIO().MousePos.x < 7))
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		ImGui::SetTooltip("%s\n%s - %s", clip.name.c_str(), FormatTime(clip.start).c_str(), FormatTime(clip.start + clip.duration).c_str());
		if (editMode_ == TimelineEditMode::Replace && !TimelineEditOperations::CanSplit(track.kind, clip)) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
			ImGui::SetTooltip("この種類はReplaceに対応していません MixかRippleを選択してください");
		}
	}
	if (ImGui::BeginPopupContextItem("ClipMenu")) {
		selectedTrack_ = track.id;
		if (!IsSelected(track.id, clip.id)) selectedClips_ = {{track.id, clip.id}};
		ImGui::BeginDisabled(track.locked);
		if (ImGui::MenuItem("Inspectorで編集")) SelectInspection(context, track.id, clip.id);
		if (ImGui::MenuItem("有効", nullptr, clip.enabled)) { clip.enabled = !clip.enabled; session_.MarkChanged(); }
		ImGui::Separator();
		if (ImGui::MenuItem("コピー", "Ctrl+C")) CopySelection();
		if (ImGui::MenuItem("複製", "Ctrl+D")) clipAction_ = ClipAction::Duplicate;
		const bool canSplit = TimelineEditOperations::CanSplit(track.kind, clip);
		if (ImGui::MenuItem("再生位置で分割", "S", false, canSplit)) clipAction_ = ClipAction::Split;
		if (!canSplit && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("分割は骨アニメーション・音声・アニメーションクリップの素材に対応しています");
		if (ImGui::MenuItem("削除", "Delete")) clipAction_ = ClipAction::Delete;
		ImGui::EndDisabled();
		ImGui::EndPopup();
	}
	ImGui::PopID();
}
