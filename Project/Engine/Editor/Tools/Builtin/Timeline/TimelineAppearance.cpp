#include "TimelineAppearance.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Common/EditorIconButton.h>

// c++
#include <array>

using namespace Engine;

namespace {

	constexpr std::array<ImU32, static_cast<size_t>(TimelineAppearance::Color::Count)> kColors{
		IM_COL32(2, 2, 2, 255), IM_COL32(5, 5, 5, 255), IM_COL32(6, 6, 6, 255),
		IM_COL32(14, 14, 14, 255), IM_COL32(10, 10, 10, 255), IM_COL32(17, 17, 17, 255),
		IM_COL32(24, 26, 29, 255), IM_COL32(62, 62, 62, 255), IM_COL32(24, 24, 24, 255),
		IM_COL32(85, 85, 85, 255), IM_COL32(225, 225, 225, 255), IM_COL32(105, 105, 105, 255),
		IM_COL32(255, 255, 255, 255), IM_COL32(240, 240, 240, 255), IM_COL32(145, 145, 145, 255),
		IM_COL32(13, 46, 115, 255), IM_COL32(13, 46, 115, 45), IM_COL32(13, 46, 115, 65),
		IM_COL32(28, 28, 28, 255)
	};
	constexpr ImGuiCol kStyleColors[]{ImGuiCol_Text, ImGuiCol_WindowBg, ImGuiCol_ChildBg,
		ImGuiCol_PopupBg, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
		ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive, ImGuiCol_TextSelectedBg,
		ImGuiCol_Separator, ImGuiCol_Border, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive};
}

ImU32 TimelineAppearance::GetColor(Color color) {

	return kColors[static_cast<size_t>(color)];
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
