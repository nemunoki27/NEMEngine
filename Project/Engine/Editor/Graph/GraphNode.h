#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphPin.h>

// c++
#include <string>
#include <vector>
// imgui
#include <imgui.h>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	GraphNode structure
	//	Graph上に配置するNode情報
	//============================================================================

	struct GraphNode {

		//--------- variables ----------------------------------------------------

		// Node ID
		GraphID id = 0;
		// Node種別
		std::string type;
		// 表示名
		std::string displayName;

		// Editor上の表示位置
		ImVec2 position{ 0.0f, 0.0f };
		// Editor上の表示サイズ
		ImVec2 size{ 0.0f, 0.0f };

		// 有効状態
		bool enabled = true;
		// 折りたたみ状態
		bool collapsed = false;

		// 入力Pin
		std::vector<GraphPin> inputs;
		// 出力Pin
		std::vector<GraphPin> outputs;

		// Toolごとの追加データ
		nlohmann::json properties = nlohmann::json::object();

		// Node単位の検証メッセージ
		std::vector<std::string> validationMessages;
	};
} // Engine
