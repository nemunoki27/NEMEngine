#include "EditorIconButton.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/EditorWindowPresentation.h>

// c++
#include <algorithm>

Engine::EditorIconButton::Colors Engine::EditorIconButton::GetColors(bool selected) {

	return selected ? Colors{ImVec4(0.05f, 0.18f, 0.45f, 1.0f), ImVec4(0.07f, 0.24f, 0.58f, 1.0f),
		ImVec4(0.10f, 0.32f, 0.74f, 1.0f)} :
		Colors{ImVec4(0.04f, 0.04f, 0.04f, 0.95f), ImVec4(0.08f, 0.08f, 0.08f, 0.98f),
		ImVec4(0.02f, 0.02f, 0.02f, 1.0f)};
}

void Engine::EditorIconButton::PushColors(bool selected) {

	const auto colors = GetColors(selected);
	ImGui::PushStyleColor(ImGuiCol_Button, colors.normal);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.hovered);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors.pressed);
}

void Engine::EditorIconButton::PopColors() {

	ImGui::PopStyleColor(3);
}

bool Engine::EditorIconButton::Draw(const char* id, const char* glyph, const char* tooltip,
	bool selected, float size) {

	if (size <= 0.0f) size = ImGui::GetFrameHeight();
	return Draw(id, glyph, tooltip, selected, ImVec2(size, size));
}

bool Engine::EditorIconButton::Draw(const char* id, const char* glyph, const char* tooltip,
	bool selected, const ImVec2& size) {

	ImGui::PushID(id);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
	PushColors(selected);
	const char* text = EditorWindowPresentation::HasIconFont() || static_cast<unsigned char>(*glyph) < 128 ? glyph : "*";
	const bool result = ImGui::Button(text, size);
	PopColors();
	ImGui::PopStyleVar(2);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
	ImGui::PopID();
	return result;
}

bool Engine::EditorIconButton::DrawImage(const char* id, ImTextureID texture, const char* tooltip, bool selected, float size) {

	return DrawImage(id, texture, tooltip, selected, ImVec2(size, size));
}

bool Engine::EditorIconButton::DrawImage(const char* id, ImTextureID texture, const char* tooltip,
	bool selected, const ImVec2& size) {

	const bool result = Draw(id, "", tooltip, selected, size);
	const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
	const float side = (std::max)(1.0f, (std::min)(size.x, size.y) - 8.0f);
	const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
	ImGui::GetWindowDrawList()->AddImage(texture, ImVec2(center.x - side * 0.5f, center.y - side * 0.5f),
		ImVec2(center.x + side * 0.5f, center.y + side * 0.5f), ImVec2(0, 0), ImVec2(1, 1),
		ImGui::GetColorU32(ImVec4(1, 1, 1, 1)));
	return result;
}
