#include "ToolPanel.h"

//============================================================================
//	ToolPanel classMethods
//============================================================================

void Engine::ToolPanel::Draw(const EditorPanelContext& context) {

	// コンソールパネルの表示状態を確認
	if (!context.layoutState->showTool) {
		return;
	}

	if (!ImGui::Begin("Tool", &context.layoutState->showTool)) {
		ImGui::End();
		return;
	}

	ImGui::End();
}