#include "NodeGraphInteraction.h"

//============================================================================
//	NodeGraphInteraction classMethods
//============================================================================

bool Engine::NodeGraphInteraction::TryCreateLink(GraphDocument& document, GraphID fromPinID, GraphID toPinID) {

	// Document側の検証を通してLinkを作成する
	return document.AddLink(fromPinID, toPinID) != nullptr;
}

bool Engine::NodeGraphInteraction::TryDeleteLink(GraphDocument& document, GraphID linkId) {

	// ViewからはIDだけを受け取り、実データ削除はDocumentへ任せる
	return document.RemoveLink(linkId);
}

bool Engine::NodeGraphInteraction::TryDeleteNode(GraphDocument& document, GraphID nodeID) {

	// Node削除時の関連Link整理もDocument側で行う
	return document.RemoveNode(nodeID);
}
