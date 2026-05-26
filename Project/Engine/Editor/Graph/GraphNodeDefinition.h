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
	//	GraphPinDefinition structure
	//	Node生成時に使うPin定義
	//============================================================================

	// ノード生成時に使うピンと既定値
	struct GraphPinDefinition {

		//--------- variables ----------------------------------------------------

		// 入力 / 出力の種別
		GraphPinKind kind = GraphPinKind::Input;
		// Pinの値型
		GraphValueType valueType = GraphValueType::Unknown;
		// 表示名
		std::string name;
		// 入力必須かどうか
		bool required = false;
		// 複数リンクを許可するか
		bool allowMultipleLinks = false;
	};

	//============================================================================
	//	GraphNodeDefinition structure
	//	Node生成時に使うNode定義
	//============================================================================

	struct GraphNodeDefinition {

		//--------- variables ----------------------------------------------------

		// Node種別
		std::string type;
		// 表示名
		std::string displayName;
		// 生成メニュー上の分類
		std::string category;
		// Nodeヘッダーなどに使用する色
		ImVec4 accentColor{ 0.35f, 0.35f, 0.35f, 1.0f };
		// 右クリックメニューから生成できるか
		bool canCreateFromMenu = true;

		// 生成するPin定義
		std::vector<GraphPinDefinition> pins;
		// Node生成時に設定するプロパティ
		nlohmann::json defaultProperties = nlohmann::json::object();
	};
} // Engine
