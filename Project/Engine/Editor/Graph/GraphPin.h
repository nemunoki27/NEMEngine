#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphTypes.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	GraphPinKind enum
	//	Pinの入出力方向
	//============================================================================

	enum class GraphPinKind {

		Input,
		Output,
	};

	//============================================================================
	//	GraphPin structure
	//	ノード間を接続するPin情報
	//============================================================================

	struct GraphPin {

		//--------- variables ----------------------------------------------------

		// Pin ID
		GraphID id = 0;
		// 所属しているNode ID
		GraphID nodeID = 0;
		// 入力 / 出力の種別
		GraphPinKind kind = GraphPinKind::Input;
		// 接続できる値の型
		GraphValueType valueType = GraphValueType::Unknown;
		// 表示名
		std::string name;
		// 必須入力かどうか
		bool required = false;
		// 複数リンクを許可するか
		bool allowMultipleLinks = false;
	};
} // Engine
