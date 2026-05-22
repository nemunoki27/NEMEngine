#include "NodeGraphInteraction.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	NodeGraphInteraction classMethods
//============================================================================

bool Engine::NodeGraphInteraction::TryCreateLink(GraphDocument& document, GraphID fromPinID, GraphID toPinID) {

	// Document側の検証を通してLinkを作成する
	const bool result = document.AddLink(fromPinID, toPinID) != nullptr;
	if (result) {
		Logger::Output(LogType::Engine, spdlog::level::debug,
			"NodeGraph: create link. from={} to={}", fromPinID, toPinID);
	}
	return result;
}

bool Engine::NodeGraphInteraction::TryDeleteLink(GraphDocument& document, GraphID linkId) {

	// ViewからはIDだけを受け取り、実データ削除はDocumentへ任せる
	const bool result = document.RemoveLink(linkId);
	if (result) {
		Logger::Output(LogType::Engine, spdlog::level::debug,
			"NodeGraph: delete link. id={}", linkId);
	}
	return result;
}

bool Engine::NodeGraphInteraction::TryDeleteNode(GraphDocument& document, GraphID nodeID) {

	// Node削除時の関連Link整理もDocument側で行う
	const bool result = document.RemoveNode(nodeID);
	if (result) {
		Logger::Output(LogType::Engine, spdlog::level::debug,
			"NodeGraph: delete node. id={}", nodeID);
	}
	return result;
}
