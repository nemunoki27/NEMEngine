#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphID.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	GraphValidationMessage structure
	//	Graph検証で発生したメッセージ
	//============================================================================
	struct GraphValidationMessage {

		//--------- enum ---------------------------------------------------------

		// メッセージの重要度
		enum class Severity {

			Info,
			Warning,
			Error,
		};

		//--------- variables ----------------------------------------------------

		// 重要度
		Severity severity = Severity::Info;
		// 対象Node ID
		GraphID nodeID = 0;
		// 対象Pin ID
		GraphID pinID = 0;
		// 表示するメッセージ
		std::string message;
	};

	//============================================================================
	//	GraphValidationResult structure
	//	Graph検証結果を保持する構造体
	//============================================================================

	struct GraphValidationResult {

		//--------- variables ----------------------------------------------------

		// 検証メッセージ一覧
		std::vector<GraphValidationMessage> messages;

		//--------- functions ----------------------------------------------------

		// Errorが含まれているか
		bool HasError() const;
		// 検証結果をクリアする
		void Clear();
		// 検証メッセージを追加する
		void Add(GraphValidationMessage::Severity severity,
			GraphID nodeID, GraphID pinID, const std::string& message);
	};
} // Engine
