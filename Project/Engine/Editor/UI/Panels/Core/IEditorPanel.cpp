#include "IEditorPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

//============================================================================
//	IEditorPanel classMethods
//============================================================================
void Engine::IEditorPanel::ConfigureInstance(const std::string& panelTypeID,
	const std::string& instanceID, bool primaryInstance) {

	panelTypeID_ = panelTypeID;
	instanceID_ = instanceID;
	primaryInstance_ = primaryInstance;
	instanceOpen_ = true;
}

std::string Engine::IEditorPanel::MakeWindowName(const std::string& displayName) const {

	if (instanceID_.empty()) {
		return displayName;
	}
	return displayName + "###" + panelTypeID_ + ":" + instanceID_;
}

void Engine::IEditorPanel::DrawTitleBarContextMenu(const EditorPanelContext& context) {

	if (instanceID_.empty() || !context.host) {
		return;
	}

	ImGuiWindow* window = ImGui::GetCurrentWindow();
	currentDockID_ = window->DockId;
	const ImRect titleBar = window->TitleBarRect();
	const std::string popupID = "##PanelTitleContext:" + instanceID_;
	if (titleBar.Contains(ImGui::GetMousePos()) && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
		ImGui::OpenPopup(popupID.c_str());
	}

	if (!ImGui::BeginPopup(popupID.c_str())) {
		return;
	}

	const bool canDuplicate = CanDuplicate(context);
	if (ImGui::MenuItem("複製", nullptr, false, canDuplicate)) {
		context.host->RequestDuplicatePanel(instanceID_);
	}
	ImGui::EndPopup();
}

bool* Engine::IEditorPanel::ResolveOpenState(bool* primaryOpenState) {

	return primaryInstance_ ? primaryOpenState : &instanceOpen_;
}

void Engine::IEditorPanel::ApplyInitialDock() {

	if (initialDockID_ == 0) {
		return;
	}
	ImGui::SetNextWindowDockID(initialDockID_, ImGuiCond_Always);
	initialDockID_ = 0;
}

//============================================================================
//	IEditorPanel classMethods
//============================================================================

namespace Engine {

	nlohmann::json IEditorPanel::MakeDuplicateState([[maybe_unused]] const EditorPanelContext& context) const {

		return SaveLayoutState();
	}
}
