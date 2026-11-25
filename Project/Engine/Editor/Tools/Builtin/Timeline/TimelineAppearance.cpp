#include "TimelineAppearance.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/EditorPalette.h>
#include <Engine/Editor/UI/Common/EditorIconButton.h>

// c++
#include <array>

using namespace Engine;

namespace {

	constexpr std::array<const char*, static_cast<size_t>(TimelineAppearance::Color::Count)> kColors{
		"Panels.Timeline.Background", "Panels.Timeline.Ruler", "Panels.Timeline.Track",
		"Panels.Timeline.SelectedTrack", "Panels.Timeline.Clip", "Panels.Timeline.ActiveClip",
		"Panels.Timeline.SelectedClip", "Panels.Timeline.Grid", "Panels.Timeline.MinorGrid",
		"Panels.Timeline.Divider", "Panels.Timeline.Text", "Panels.Timeline.MutedText",
		"Panels.Timeline.Playhead", "Panels.Timeline.Selection", "Panels.Timeline.Blend",
		"Panels.Timeline.Accent", "Panels.Timeline.Range", "Panels.Timeline.SelectionFill",
		"Panels.Timeline.Zoom"
	};
	constexpr ImGuiCol kStyleColors[]{ImGuiCol_Text, ImGuiCol_WindowBg, ImGuiCol_ChildBg,
		ImGuiCol_PopupBg, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
		ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive, ImGuiCol_TextSelectedBg,
		ImGuiCol_Separator, ImGuiCol_Border, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive};
}

ImU32 TimelineAppearance::GetColor(Color color) {

	return Engine::EditorPalette::U32(kColors[static_cast<size_t>(color)]);
}

void TimelineAppearance::PushStyle() {

	const auto button = EditorIconButton::GetColors(true);
	const ImVec4 colors[]{ImGui::ColorConvertU32ToFloat4(GetColor(Color::Text)),
		ImGui::ColorConvertU32ToFloat4(GetColor(Color::Background)),
		ImGui::ColorConvertU32ToFloat4(GetColor(Color::Background)),
		ImGui::ColorConvertU32ToFloat4(GetColor(Color::Ruler)),
		ImGui::ColorConvertU32ToFloat4(GetColor(Color::Clip)), button.normal, button.hovered,
		button.normal, button.hovered, button.pressed,
		button.hovered, ImGui::ColorConvertU32ToFloat4(GetColor(Color::Divider)),
		ImGui::ColorConvertU32ToFloat4(GetColor(Color::MinorGrid)), button.hovered, button.pressed};
	for (size_t i = 0; i < std::size(kStyleColors); ++i) ImGui::PushStyleColor(kStyleColors[i], colors[i]);
}

void TimelineAppearance::PopStyle() {

	ImGui::PopStyleColor(static_cast<int>(std::size(kStyleColors)));
}
