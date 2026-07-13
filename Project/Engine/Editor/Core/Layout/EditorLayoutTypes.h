#pragma once

//============================================================================
//	include
//============================================================================
#include <json.hpp>

// c++
#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	EditorLayout structures
	//============================================================================
	// パネルの表示状態
	struct EditorPanelVisibility {

		bool showHierarchy = true;
		bool showInspector = true;
		bool showProject = true;
		bool showConsole = true;
		bool showSceneView = true;
		bool showGameView = true;
		bool showToolbar = true;
		bool showTool = true;
	};

	// パネルインスタンスの保存状態
	struct EditorPanelLayoutSnapshot {

		std::string typeID;
		std::string instanceID;
		bool primary = false;
		bool open = true;
		nlohmann::json state = nlohmann::json::object();
	};

	// 1つのエディターレイアウト
	struct EditorLayoutSnapshot {

		std::string layoutID;
		std::string displayName;
		int32_t order = 0;
		bool builtinDefault = false;
		EditorPanelVisibility visibility{};
		std::vector<EditorPanelLayoutSnapshot> panels;
		std::string imguiIniData;
	};

	// レイアウト一覧の表示情報
	struct EditorLayoutMenuEntry {

		std::string layoutID;
		std::string displayName;
		bool defaultLayout = false;
		bool imported = false;
	};
} // Engine
