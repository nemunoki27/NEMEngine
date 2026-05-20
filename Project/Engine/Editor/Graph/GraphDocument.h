#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphNode.h>
#include <Engine/Editor/Graph/GraphLink.h>

// c++
#include <string>
#include <vector>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	GraphDocument structure
	//	Editor上で編集する汎用Node Graph
	//============================================================================

	struct GraphDocument {

		//--------- variables ----------------------------------------------------

		// Graphの用途種別
		std::string graphType;
		// 保存形式のバージョン
		uint32_t version = 1;

		// Graph上に配置されているNode
		std::vector<GraphNode> nodes;
		// Node間を接続しているLink
		std::vector<GraphLink> links;

		// 次に割り当てるID
		GraphID nextId = 1;

		// Toolごとの追加情報
		nlohmann::json metadata = nlohmann::json::object();

		//--------- functions ----------------------------------------------------

		// 新しいGraph IDを発行する
		GraphID GenerateID();

		// IDからNodeを検索する
		GraphNode* FindNode(GraphID id);
		// IDからNodeを検索する
		const GraphNode* FindNode(GraphID id) const;
		// IDからPinを検索する
		GraphPin* FindPin(GraphID id);
		// IDからPinを検索する
		const GraphPin* FindPin(GraphID id) const;
		// IDからLinkを検索する
		GraphLink* FindLink(GraphID id);
		// IDからLinkを検索する
		const GraphLink* FindLink(GraphID id) const;

		// Nodeを追加する
		GraphNode& AddNode(GraphNode node);
		// Nodeを削除する
		bool RemoveNode(GraphID id);
		// Linkを作成できるか判定する
		bool CanCreateLink(GraphID fromPinID, GraphID toPinID, std::string* reason = nullptr) const;
		// Linkを追加する
		GraphLink* AddLink(GraphID fromPinID, GraphID toPinID);
		// Linkを削除する
		bool RemoveLink(GraphID id);
		// 指定したPinへ接続済みのLinkがあるか
		bool HasLinkToPin(GraphID pinID) const;
		// Nodeに残っている検証メッセージをクリアする
		void ClearValidationMessages();
	};
} // Engine
