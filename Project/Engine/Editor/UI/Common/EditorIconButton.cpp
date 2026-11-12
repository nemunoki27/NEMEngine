#include "EditorIconButton.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/EditorWindowPresentation.h>

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
	ImGui::PushID(id);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
	PushColors(selected);
	const bool result = ImGui::Button(EditorWindowPresentation::HasIconFont() ? glyph : "*", ImVec2(size, size));
	PopColors();
	ImGui::PopStyleVar(2);
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tooltip);
	ImGui::PopID();
	return result;
}
