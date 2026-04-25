#include "ToolbarPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanelHost.h>

//============================================================================
//	ToolbarPanel classMethods
//============================================================================

void Engine::ToolbarPanel::Draw(const EditorPanelContext& context) {

	// ツールバーパネルの表示状態を確認
	if (!context.layoutState->showToolbar) {
		return;
	}

	// ツールバーウィンドウのフラグ
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;

	if (!ImGui::Begin("Toolbar", &context.layoutState->showToolbar, flags)) {
		ImGui::End();
		return;
	}

	ImGui::SetWindowFontScale(0.64f);

	//============================================================================
	//	シーンのプレイ/ストップ切り替え
	//============================================================================

	const char* playLabel = context.IsPlaying() ? "Stop" : "Play";
	if (ImGui::Button(playLabel, ImVec2(64.0f, 20.0f))) {
		context.host->RequestPlayToggle();
	}

	ImGui::SameLine();
	ImGui::TextDisabled("F5");

	ImGui::SameLine(0.0f, 12.0f);
	if (context.layoutState) {
		ImGui::Checkbox("Wait Managed Debugger", &context.layoutState->waitForManagedDebuggerOnPlay);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Play start waits for managed debugger attach during GameScripts load.");
		}
	}

	ImGui::SameLine(0.0f, 20.0f);
	ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);

	//============================================================================
	//	現在のシーン名、モード、Undo/Redoの状態を表示
	//============================================================================

	ImGui::SameLine();
	if (context.editorContext && context.editorContext->activeSceneHeader) {

		ImGui::Text("Scene : %s", context.editorContext->activeSceneHeader->name.c_str());
	} else {

		ImGui::Text("Scene : <None>");
	}

	ImGui::SameLine(0.0f, 16.0f);
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	ImGui::Text("Mode : %s", context.IsPlaying() ? "Play" : "Edit");

	ImGui::SameLine(0.0f, 16.0f);
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	ImGui::Text("Undo : %zu / Redo : %zu", context.editorState->commandHistory.GetUndoCount(),
		context.editorState->commandHistory.GetRedoCount());

	ImGui::SetWindowFontScale(1.0f);

	ImGui::End();
}
