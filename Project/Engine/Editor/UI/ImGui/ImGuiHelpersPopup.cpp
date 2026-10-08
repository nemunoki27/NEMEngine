#include "ImGuiHelpers.h"

//============================================================================
//	include
//============================================================================
// c++
#include <cfloat>

//============================================================================
//	MyGUI classMethods
//============================================================================
bool Engine::MyGUI::BeginPopupModal(const char* name, bool* open,
	ImGuiWindowFlags flags, float width) {

	// 高さは従来の自動調整を使う
	const float maximumWidth = (flags & ImGuiWindowFlags_AlwaysAutoResize) != 0 ? width : FLT_MAX;
	ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(maximumWidth, FLT_MAX));
	return ImGui::BeginPopupModal(name, open, flags);
}
