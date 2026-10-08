#include "EditorManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolUI.h>

// c++
#include <algorithm>

bool Engine::EditorManager::HasPendingPanelEdits() const {

	return EditorToolUI::HasPendingEdits() ||
		   std::any_of(panels_.begin(), panels_.end(),
			   [](const std::unique_ptr<IEditorPanel>& panel) { return panel->HasPendingEdits(); });
}

void Engine::EditorManager::RequestResolvePendingPanelEdits() {

	pendingPanelEditResolution_ = true;
	EditorToolUI::RequestResolvePendingEdits();
	for (const std::unique_ptr<IEditorPanel>& panel : panels_) {
		if (panel->HasPendingEdits()) {
			panel->RequestResolvePendingEdits();
		}
	}
}

Engine::EditorPanelCloseResult Engine::EditorManager::ConsumePendingPanelEditResult() {

	if (!pendingPanelEditResolution_) {
		return EditorPanelCloseResult::None;
	}
	bool accepted = false;
	const EditorToolCloseResult toolResult = EditorToolUI::ConsumePendingEditCloseResult();
	if (toolResult == EditorToolCloseResult::Cancelled) {
		pendingPanelEditResolution_ = false;
		return EditorPanelCloseResult::Cancelled;
	}
	accepted |= toolResult == EditorToolCloseResult::Accepted;
	for (const std::unique_ptr<IEditorPanel>& panel : panels_) {
		const EditorPanelCloseResult result = panel->ConsumePendingEditCloseResult();
		if (result == EditorPanelCloseResult::Cancelled) {
			pendingPanelEditResolution_ = false;
			return result;
		}
		accepted |= result == EditorPanelCloseResult::Accepted;
	}
	if (HasPendingPanelEdits()) {
		return EditorPanelCloseResult::None;
	}
	pendingPanelEditResolution_ = false;
	return accepted ? EditorPanelCloseResult::Accepted : EditorPanelCloseResult::None;
}
