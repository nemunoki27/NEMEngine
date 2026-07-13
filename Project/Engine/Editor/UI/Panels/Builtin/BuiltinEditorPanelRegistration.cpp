#include "BuiltinEditorPanelRegistration.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

// パネル群
#include <Engine/Editor/UI/Panels/Builtin/MenuBarPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/ToolbarPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/HierarchyPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/InspectorPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/ConsolePanel.h>
#include <Engine/Editor/UI/Panels/Builtin/ToolPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/ProjectPanel.h>
#include <Engine/Editor/UI/Panels/Builtin/ViewportPanel.h>

//============================================================================
//	BuiltinEditorPanelRegistration methods
//============================================================================
std::vector<std::unique_ptr<Engine::IEditorPanel>> Engine::CreateBuiltinEditorPanels(
	const EditorPanelCreateContext& context) {

	TextureUploadService& textureUploadService = context.textureUploadService;

	// 表示順は既存のInitと同一に保つ、順番を変えるとレイアウト復元に影響する
	std::vector<std::unique_ptr<IEditorPanel>> panels;
	panels.emplace_back(std::make_unique<MenuBarPanel>());
	panels.emplace_back(std::make_unique<ToolbarPanel>(textureUploadService));
	panels.emplace_back(std::make_unique<HierarchyPanel>(textureUploadService));
	panels.emplace_back(std::make_unique<InspectorPanel>());
	panels.emplace_back(std::make_unique<ConsolePanel>());
	panels.emplace_back(std::make_unique<ToolPanel>());
	panels.emplace_back(std::make_unique<ProjectPanel>(textureUploadService));
	panels.emplace_back(std::make_unique<ViewportPanel>("GameView", "GameView", ViewportPanelKind::Game, textureUploadService));
	panels.emplace_back(std::make_unique<ViewportPanel>("SceneView", "SceneView", ViewportPanelKind::Scene, textureUploadService));
	return panels;
}

std::unique_ptr<Engine::IEditorPanel> Engine::CreateBuiltinEditorPanelInstance(
	const EditorPanelCreateContext& context, const std::string& typeID,
	const std::string& instanceID, const std::string& displayName) {

	if (typeID == "Project") {
		return std::make_unique<ProjectPanel>(context.textureUploadService,
			instanceID, false, displayName.empty() ? "Project" : displayName);
	}
	if (typeID == "Inspector") {
		return std::make_unique<InspectorPanel>(instanceID, false);
	}
	return nullptr;
}
