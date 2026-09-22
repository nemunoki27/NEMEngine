#include "EditorLayoutSerialization.h"

namespace {

	nlohmann::json MakeVisibilityJson(const Engine::EditorPanelVisibility& visibility) {

		return {
			{ "hierarchy", visibility.showHierarchy },
			{ "inspector", visibility.showInspector },
			{ "project", visibility.showProject },
			{ "console", visibility.showConsole },
			{ "sceneView", visibility.showSceneView },
			{ "gameView", visibility.showGameView },
			{ "toolbar", visibility.showToolbar },
			{ "tool", visibility.showTool },
		};
	}

	Engine::EditorPanelVisibility ReadVisibility(const nlohmann::json& data) {

		Engine::EditorPanelVisibility visibility{};
		if (!data.is_object()) {
			return visibility;
		}

		visibility.showHierarchy = data.value("hierarchy", visibility.showHierarchy);
		visibility.showInspector = data.value("inspector", visibility.showInspector);
		visibility.showProject = data.value("project", visibility.showProject);
		visibility.showConsole = data.value("console", visibility.showConsole);
		visibility.showSceneView = data.value("sceneView", visibility.showSceneView);
		visibility.showGameView = data.value("gameView", visibility.showGameView);
		visibility.showToolbar = data.value("toolbar", visibility.showToolbar);
		visibility.showTool = data.value("tool", visibility.showTool);
		return visibility;
	}
}

nlohmann::json Engine::EditorLayoutSerialization::MakeLayoutJson(const Engine::EditorLayoutSnapshot& layout, bool imported) {

	nlohmann::json panels = nlohmann::json::array();
	for (const Engine::EditorPanelLayoutSnapshot& panel : layout.panels) {

		panels.push_back({
			{ "typeID", panel.typeID },
			{ "instanceID", panel.instanceID },
			{ "primary", panel.primary },
			{ "open", panel.open },
			{ "state", panel.state },
			});
	}

	return {
		{ "layoutID", layout.layoutID },
		{ "displayName", layout.displayName },
		{ "order", layout.order },
		{ "builtinDefault", layout.builtinDefault },
		{ "imported", imported },
		{ "visibility", MakeVisibilityJson(layout.visibility) },
		{ "panels", panels },
		{ "imguiIniData", layout.imguiIniData },
	};
}

bool Engine::EditorLayoutSerialization::ReadLayout(const nlohmann::json& data, Engine::EditorLayoutSnapshot& outLayout, bool& outImported) {

	if (!data.is_object()) {
		return false;
	}

	outLayout.layoutID = data.value("layoutID", std::string{});
	outLayout.displayName = data.value("displayName", std::string{});
	if (outLayout.layoutID.empty() || outLayout.displayName.empty()) {
		return false;
	}

	outLayout.order = data.value("order", 0);
	outLayout.builtinDefault = data.value("builtinDefault", false);
	outImported = data.value("imported", false);
	outLayout.visibility = ReadVisibility(data.value("visibility", nlohmann::json::object()));
	outLayout.imguiIniData = data.value("imguiIniData", std::string{});
	outLayout.panels.clear();

	const nlohmann::json panels = data.value("panels", nlohmann::json::array());
	if (panels.is_array()) {
		for (const nlohmann::json& panelData : panels) {

			if (!panelData.is_object()) {
				continue;
			}

			Engine::EditorPanelLayoutSnapshot panel{};
			panel.typeID = panelData.value("typeID", std::string{});
			panel.instanceID = panelData.value("instanceID", std::string{});
			if (panel.typeID.empty() || panel.instanceID.empty()) {
				continue;
			}
			panel.primary = panelData.value("primary", false);
			panel.open = panelData.value("open", true);
			panel.state = panelData.value("state", nlohmann::json::object());
			outLayout.panels.emplace_back(std::move(panel));
		}
	}
	return true;
}
