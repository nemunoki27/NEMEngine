#include "TimelineTool.h"
#include "TimelineAppearance.h"
#include "TimelineEditorFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Editor/UI/Common/EditorIconButton.h>

// c++
#include <algorithm>
#include <iterator>

using namespace Engine;
using TimelineAppearance::Color;
using TimelineAppearance::GetColor;

namespace {

	const char* TrackIcon(TimelineTrackKind kind) {

		const char* icons[]{"\xef\x81\xbb", "\xef\x86\x83", "\xef\x82\xb2", "\xef\x83\x90", "\xef\x86\xb2",
			"\xef\x81\x9b", "\xef\x80\xb0", "\xef\x80\xa8", "\xef\x87\x9e", "\xef\x81\xae", "\xef\x80\x97",
			"\xef\x80\xae", "\xef\x83\x88", "\xee\x8a\x9c", "\xef\x84\xa1"};
		return icons[(std::min)(static_cast<size_t>(kind), std::size(icons) - 1)];
	}
}

void TimelineTool::DrawTrackRow(const EditorToolContext& context, TimelineTrack& track, int depth,
	ImVec2 origin, float width, float timeX, float rowHeight) {

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const bool selected = selectedTrack_ == track.id;
	const float indent = 12.0f * static_cast<float>(depth);
	const bool property = track.kind == TimelineTrackKind::Property;
	const float button = (std::min)({22.0f, rowHeight - 4.0f, (labelWidth_ - 40.0f) / (property ? 5.0f : 3.0f)});
	const float controls = button * (property ? 5 : 3);
	const float textX = origin.x + 28 + indent;
	const float textWidth = (std::max)(8.0f, labelWidth_ - 32 - indent - controls);
	draw->AddRectFilled(origin, ImVec2(timeX, origin.y + rowHeight),
		selected ? GetColor(Color::SelectedTrack) : GetColor(Color::Track));
	draw->AddRectFilled(ImVec2(origin.x, origin.y + 1), ImVec2(origin.x + 3, origin.y + rowHeight - 1),
		TimelineEditorFields::TrackColor(track.kind));
	draw->AddLine(ImVec2(origin.x, origin.y + rowHeight), ImVec2(origin.x + width, origin.y + rowHeight), GetColor(Color::MinorGrid));
	ImGui::PushID(ToString(track.id).c_str());
	ImGui::SetCursorScreenPos(ImVec2(origin.x + 5 + indent, origin.y + 8));
	if (track.kind == TimelineTrackKind::Group) {
		if (EditorIconButton::Draw("Expand", collapsedGroups_.contains(track.id) ? "\xef\x81\x94" : "\xef\x81\xb8",
			"グループを展開・折りたたみ", false, 18.0f)) {
			if (collapsedGroups_.contains(track.id)) collapsedGroups_.erase(track.id);
			else collapsedGroups_.insert(track.id);
		}
	} else draw->AddText(ImVec2(origin.x + 6 + indent, origin.y + 10), TimelineEditorFields::TrackColor(track.kind), TrackIcon(track.kind));
	ImGui::SetCursorScreenPos(ImVec2(textX, origin.y + 1));
	ImGui::InvisibleButton("Track", ImVec2(textWidth, 17.0f));
	if (ImGui::IsItemClicked()) {
		selectedTrack_ = track.id;
		selectedClips_.clear();
		SelectInspection(context, track.id);
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("TrackMenu");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", track.name.c_str(), TimelineEditorFields::TrackLabel(track.kind));
	if (!track.locked && ImGui::BeginDragDropSource()) {
		ImGui::SetDragDropPayload("NEM_TIMELINE_TRACK", &track.id, sizeof(track.id));
		ImGui::TextUnformatted(track.name.c_str());
		ImGui::EndDragDropSource();
	}
	if (ImGui::BeginDragDropTarget()) {
		if (const auto* payload = ImGui::AcceptDragDropPayload("NEM_TIMELINE_TRACK"); payload && payload->DataSize == sizeof(UUID)) {
			movedTrack_ = *static_cast<const UUID*>(payload->Data);
			moveDestination_ = track.id;
			const auto source = std::find_if(visibleTracks_.begin(), visibleTracks_.end(),
				[&](const auto& value) { return value.id == movedTrack_; });
			const bool downward = source != visibleTracks_.end() && source->y < origin.y - canvasOrigin_.y;
			movePosition_ = track.kind == TimelineTrackKind::Group && !ImGui::GetIO().KeyShift ?
				TimelineTrackDropPosition::IntoGroup : downward ? TimelineTrackDropPosition::After : TimelineTrackDropPosition::Before;
		}
		ImGui::EndDragDropTarget();
	}
	const ImVec4 clip(textX, origin.y, textX + textWidth, origin.y + 18);
	draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(textX + 1, origin.y + 1),
		track.muted ? GetColor(Color::MutedText) : GetColor(Color::Text), track.name.c_str(), nullptr, 0, &clip);
	if (track.kind == TimelineTrackKind::Group) {
		const char* label = track.fallback ? "それ以外の分岐" : track.conditions.empty() ? "グループ" : "条件付きグループ";
		draw->AddText(ImVec2(textX, origin.y + 19), GetColor(Color::MutedText), label);
	} else DrawTrackBinding(context, track, ImVec2(textX, origin.y + 18), textWidth);
	ImGui::SetCursorScreenPos(ImVec2(timeX - controls - 2, origin.y + (rowHeight - button) * 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	if (property) {
		ImGui::BeginDisabled(track.locked || context.IsPlaying());
		if (IsTrackRecording(track.id)) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.25f, 1.0f));
		const bool recording = EditorIconButton::Draw("Record", "\xef\x84\x91", "現在位置へキーを記録", IsTrackRecording(track.id), button);
		if (IsTrackRecording(track.id)) ImGui::PopStyleColor();
		if (recording) ToggleTrackRecording(context, track.id);
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (EditorIconButton::Draw("Curves", "\xef\x88\x81", "キーとカーブを表示", expandedCurves_.contains(track.id), button)) {
			if (expandedCurves_.contains(track.id)) expandedCurves_.erase(track.id);
			else expandedCurves_.insert(track.id);
		}
		ImGui::SameLine();
	}
	if (EditorIconButton::Draw("Mute", track.muted ? "\xef\x84\xb1" : "\xef\x80\xa8", "ミュート", track.muted, button)) {
		track.muted = !track.muted; session_.MarkChanged();
	}
	ImGui::SameLine();
	if (EditorIconButton::Draw("Lock", track.locked ? "\xef\x80\xa3" : "\xef\x82\x9c", "編集をロック", track.locked, button)) {
		track.locked = !track.locked; session_.MarkChanged();
	}
	ImGui::SameLine();
	if (EditorIconButton::Draw("More", "\xef\x85\x82", "トラックのメニュー", false, button)) ImGui::OpenPopup("TrackMenu");
	ImGui::PopStyleVar();
	bool remove = false, duplicate = false, add = false;
	if (ImGui::BeginPopup("TrackMenu")) {
		if (ImGui::MenuItem("Inspectorで編集")) SelectInspection(context, track.id);
		ImGui::Separator();
		ImGui::BeginDisabled(track.locked);
		if (track.kind != TimelineTrackKind::Group) {
			const bool canAdd = editMode_ != TimelineEditMode::Replace || TimelineEditOperations::CanSplit(track.kind, TimelineClip{});
			if (ImGui::MenuItem("クリップを追加", nullptr, false, canAdd)) add = true;
			if (!canAdd && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("この種類の新規クリップはMixかRippleで追加してください");
		}
		if (ImGui::MenuItem("トラックを複製")) duplicate = true;
		if (ImGui::MenuItem("トラックを削除")) remove = true;
		ImGui::EndDisabled();
		ImGui::EndPopup();
	}
	ImGui::PopID();
	if (remove) { RemoveTrack(track.id); return; }
	if (duplicate) { DuplicateTrack(track.id); return; }
	if (add) {
		if (AddClip(track)) SelectInspection(context, selectedTrack_, selectedClips_.front().clip);
		return;
	}
	draw->PushClipRect(ImVec2(timeX, origin.y), ImVec2(origin.x + width, origin.y + rowHeight), true);
	for (auto& value : track.clips) DrawClip(context, track, value, origin, timeX, width - labelWidth_, rowHeight);
	draw->PopClipRect();
}
