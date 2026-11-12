#include "TimelineTool.h"
#include "TimelineAppearance.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Common/EditorIconButton.h>

// c++
#include <algorithm>
#include <cmath>
#include <format>

using namespace Engine;
using TimelineAppearance::Color;
using TimelineAppearance::GetColor;

void TimelineTool::DrawEditToolbar(const EditorToolContext& context) {

	const float size = ImGui::GetFrameHeight();
	if (EditorIconButton::Draw("AddTrack", "\xef\x81\xa7", "トラックを追加", false, size)) ImGui::OpenPopup("TimelineAddTrack");
	if (ImGui::BeginPopup("TimelineAddTrack")) { DrawAddTrackMenu(context); ImGui::EndPopup(); }
	ImGui::SameLine();
	const char* icons[]{"\xef\x97\xbd", "\xef\x81\xa1", "\xef\x8d\xa2"};
	const char* tips[]{"Mix: 重ねて配置", "Ripple: 後続を連動して配置", "Replace: 重なった素材を置き換え"};
	if (labelWidth_ < 200.0f) {
		if (EditorIconButton::Draw("EditModes", icons[static_cast<int>(editMode_)], tips[static_cast<int>(editMode_)], true, size))
			ImGui::OpenPopup("TimelineEditModes");
		if (ImGui::BeginPopup("TimelineEditModes")) {
			for (int mode = 0; mode < 3; ++mode)
				if (ImGui::MenuItem(tips[mode], nullptr, static_cast<int>(editMode_) == mode)) editMode_ = static_cast<TimelineEditMode>(mode);
			ImGui::EndPopup();
		}
		ImGui::SameLine();
	} else for (int mode = 0; mode < 3; ++mode) {
		ImGui::PushID(mode);
		if (EditorIconButton::Draw("EditMode", icons[mode], tips[mode], static_cast<int>(editMode_) == mode, size))
			editMode_ = static_cast<TimelineEditMode>(mode);
		ImGui::PopID();
		ImGui::SameLine();
	}
	if (EditorIconButton::Draw("Snap", "\xef\x81\xb6", "フレームと端点へスナップ Shiftで一時解除", snap_, size)) snap_ = !snap_;
	ImGui::SameLine();
	if (labelWidth_ > size * 7.0f) {
		if (EditorIconButton::Draw("Events", "\xef\x82\x8d", "イベントマーカーを表示", showEvents_, size)) showEvents_ = !showEvents_;
		ImGui::SameLine();
	}
	if (EditorIconButton::Draw("TimelineSettings", "\xef\x87\x9e", "タイムラインの設定をInspectorに表示", false, size))
		SelectInspection(context);
}

double TimelineTool::GetTickStep() const {

	const double frame = 1.0 / session_.GetAsset().frameRate;
	const double steps[]{frame, frame * 2, frame * 5, frame * 10, frame * 20, frame * 30,
		1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 300.0, 600.0, 1800.0};
	for (const double candidate : steps) if (candidate * pixelsPerSecond_ >= 60.0) return candidate;
	return 3600.0;
}

std::string TimelineTool::FormatTime(double time) const {

	if (timeDisplay_ == TimeDisplay::Seconds) return std::format("{:.2f}", time);
	const auto frame = static_cast<int64_t>(std::llround(time * session_.GetAsset().frameRate));
	if (timeDisplay_ == TimeDisplay::Frames) return std::format("{}", frame);
	const auto fps = static_cast<int64_t>((std::max)(1.0, std::round(session_.GetAsset().frameRate)));
	return std::format("{:02}:{:02}:{:02}", frame / fps / 60, frame / fps % 60, frame % fps);
}

void TimelineTool::DrawRuler(const EditorToolContext& context, float height) {

	const double rangeFrame = (std::min)(1.0 / session_.GetAsset().frameRate, session_.GetAsset().duration);
	session_.rangeEnd = std::clamp(session_.rangeEnd, rangeFrame, session_.GetAsset().duration);
	session_.rangeStart = std::clamp(session_.rangeStart, 0.0, (std::max)(0.0, session_.rangeEnd - rangeFrame));
	ImGui::BeginChild("TimelineRuler", ImVec2(0, height), ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float width = ImGui::GetContentRegionAvail().x;
	const float timeX = origin.x + labelWidth_;
	const float laneWidth = (std::max)(width - labelWidth_, 1.0f);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), GetColor(Color::Ruler));
	DrawEditToolbar(context);
	ImGui::SetCursorScreenPos(ImVec2(timeX - 3.0f, origin.y));
	ImGui::InvisibleButton("TrackDivider", ImVec2(6.0f, height));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	if (ImGui::IsItemActive()) {
		const float minimum = (std::min)(180.0f, (std::max)(96.0f, width * 0.5f));
		labelWidth_ = std::clamp(labelWidth_ + ImGui::GetIO().MouseDelta.x, minimum, (std::max)(minimum, width * 0.7f));
	}
	draw->AddLine(ImVec2(timeX, origin.y), ImVec2(timeX, origin.y + height), GetColor(Color::Divider));
	draw->PushClipRect(ImVec2(timeX, origin.y), ImVec2(origin.x + width, origin.y + height), true);
	const double step = GetTickStep();
	const double endTime = viewStart_ + laneWidth / pixelsPerSecond_;
	for (double time = std::ceil(viewStart_ / step) * step; time <= endTime; time += step) {
		const float x = timeX + static_cast<float>((time - viewStart_) * pixelsPerSecond_);
		draw->AddLine(ImVec2(x, origin.y + height - 10), ImVec2(x, origin.y + height), GetColor(Color::Selection));
		const std::string label = FormatTime(time);
		draw->AddText(ImVec2(x + 3, origin.y + 7), GetColor(Color::Text), label.c_str());
		for (int minor = 1; minor < 5; ++minor) {
			const float mark = x + static_cast<float>(minor * step / 5.0 * pixelsPerSecond_);
			draw->AddLine(ImVec2(mark, origin.y + height - 5), ImVec2(mark, origin.y + height), GetColor(Color::MinorGrid));
		}
	}
	const float durationX = timeX + static_cast<float>((session_.GetAsset().duration - viewStart_) * pixelsPerSecond_);
	draw->AddLine(ImVec2(timeX, origin.y + 2), ImVec2(durationX, origin.y + 2), GetColor(Color::Accent), 3.0f);
	draw->AddTriangleFilled(ImVec2(durationX - 5, origin.y), ImVec2(durationX + 5, origin.y),
		ImVec2(durationX, origin.y + 8), GetColor(Color::Accent));
	if (session_.rangeEnabled) {
		const float left = timeX + static_cast<float>((session_.rangeStart - viewStart_) * pixelsPerSecond_);
		const float right = timeX + static_cast<float>((session_.rangeEnd - viewStart_) * pixelsPerSecond_);
		draw->AddRectFilled(ImVec2(left, origin.y + 4), ImVec2(right, origin.y + height), GetColor(Color::Range));
		for (const float x : {left, right}) {
			draw->AddLine(ImVec2(x, origin.y + 4), ImVec2(x, origin.y + height), GetColor(Color::Accent), 2.0f);
			draw->AddRectFilled(ImVec2(x - 4, origin.y + 4), ImVec2(x + 4, origin.y + 10), GetColor(Color::Accent));
		}
	}
	const float headX = timeX + static_cast<float>((session_.time - viewStart_) * pixelsPerSecond_);
	draw->AddLine(ImVec2(headX, origin.y + 8), ImVec2(headX, origin.y + height), GetColor(Color::Playhead));
	draw->AddTriangleFilled(ImVec2(headX - 5, origin.y + 8), ImVec2(headX + 5, origin.y + 8),
		ImVec2(headX, origin.y + 17), GetColor(Color::Playhead));
	draw->PopClipRect();
	ImGui::SetCursorScreenPos(ImVec2(timeX, origin.y));
	ImGui::InvisibleButton("TimelineScrub", ImVec2(laneWidth, height));
	if (ImGui::IsItemClicked()) {
		rangeDrag_ = 0;
		const auto& mouse = ImGui::GetIO().MousePos;
		if (session_.GetAsset().durationMode == TimelineDurationMode::FixedLength &&
			mouse.y < origin.y + 7 && std::abs(mouse.x - durationX) <= 7) rangeDrag_ = 3;
		else if (session_.rangeEnabled) {
			const double hit = viewStart_ + (mouse.x - timeX) / pixelsPerSecond_;
			if (std::abs(hit - session_.rangeStart) * pixelsPerSecond_ <= 6) rangeDrag_ = 1;
			else if (std::abs(hit - session_.rangeEnd) * pixelsPerSecond_ <= 6) rangeDrag_ = 2;
		}
	}
	if (ImGui::IsItemActive() || (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))) {
		double value = (std::max)(0.0, viewStart_ + (ImGui::GetIO().MousePos.x - timeX) / pixelsPerSecond_);
		const double frame = 1.0 / session_.GetAsset().frameRate;
		if (rangeDrag_ != 0 && snap_ && !ImGui::GetIO().KeyShift) value = std::round(value / frame) * frame;
		if (rangeDrag_ == 1) session_.rangeStart = std::clamp(value, 0.0, (std::max)(0.0, session_.rangeEnd - frame));
		else if (rangeDrag_ == 2) session_.rangeEnd = std::clamp(value, session_.rangeStart + rangeFrame, session_.GetAsset().duration);
		else if (rangeDrag_ == 3) {
			session_.GetAsset().duration = (std::max)(frame, value);
			session_.MarkChanged();
		} else session_.Seek(context, value);
	}
	if (ImGui::IsItemDeactivated()) rangeDrag_ = 0;
	ImGui::EndChild();
}

void TimelineTool::DrawZoomBar() {

	const float height = ImGui::GetFontSize() + 3.0f;
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float width = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - labelWidth_);
	const float timeX = origin.x + labelWidth_;
	const double visible = width / pixelsPerSecond_;
	const double total = (std::max)(session_.GetAsset().duration * 1.1, viewStart_ + visible);
	const float left = timeX + static_cast<float>(viewStart_ / total * width);
	const float right = timeX + static_cast<float>((viewStart_ + visible) / total * width);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(ImVec2(timeX, origin.y), ImVec2(timeX + width, origin.y + height), GetColor(Color::Background));
	draw->AddRectFilled(ImVec2(left, origin.y + 2), ImVec2(right, origin.y + height - 2), GetColor(Color::Zoom), 2.0f);
	const float head = timeX + static_cast<float>(session_.time / total * width);
	draw->AddLine(ImVec2(head, origin.y + 2), ImVec2(head, origin.y + height - 2), GetColor(Color::Playhead));
	for (int handle = 0; handle < 3; ++handle) {
		ImGui::PushID(handle);
		const float x = handle == 0 ? left : handle == 1 ? right - 6.0f : left + 6.0f;
		const float itemWidth = handle == 2 ? (std::max)(1.0f, right - left - 12.0f) : 6.0f;
		ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
		ImGui::InvisibleButton("ZoomHandle", ImVec2(itemWidth, height));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
			ImGui::SetMouseCursor(handle == 2 ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_ResizeEW);
		if (ImGui::IsItemActive()) {
			const double delta = ImGui::GetIO().MouseDelta.x / width * total;
			if (handle == 2) viewStart_ = (std::max)(0.0, viewStart_ + delta);
			else {
				const double begin = handle == 0 ? std::clamp(viewStart_ + delta, 0.0, viewStart_ + visible - 0.01) : viewStart_;
				const double end = handle == 1 ? (std::max)(begin + 0.01, viewStart_ + visible + delta) : viewStart_ + visible;
				pixelsPerSecond_ = (std::clamp)(static_cast<float>(width / (end - begin)), 8.0f, 3000.0f);
				viewStart_ = begin;
			}
		}
		ImGui::PopID();
	}
	ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
	ImGui::Dummy(ImVec2(1, 1));
}
