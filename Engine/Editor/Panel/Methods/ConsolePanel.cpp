#include "ConsolePanel.h"

//============================================================================
//	ConsolePanel classMethods
//============================================================================

void Engine::ConsolePanel::Draw(const EditorPanelContext& context) {

	// コンソールパネルの表示状態を確認
	if (!context.layoutState->showConsole) {
		return;
	}

	if (!ImGui::Begin("Console", &context.layoutState->showConsole)) {
		ImGui::End();
		return;
	}

	if (ImGui::BeginTabBar("ConsoleTabBar")) {
		//============================================================================
		//	ゲームログ
		//============================================================================
		if (ImGui::BeginTabItem("Game")) {


			ImGui::EndTabItem();
		}
		//============================================================================
		//	エンジンログ
		//============================================================================
		if (ImGui::BeginTabItem("Engine")) {


			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}