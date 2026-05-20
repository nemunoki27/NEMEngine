#include "GraphDocument.h"

// c++
#include <algorithm>

//============================================================================
//	GraphDocument classMethods
//============================================================================

Engine::GraphID Engine::GraphDocument::GenerateID() {

	// 0は無効値として残す
	if (nextId == 0) {
		nextId = 1;
	}
	return nextId++;
}

Engine::GraphNode* Engine::GraphDocument::FindNode(GraphID id) {

	// Nodeは数が極端に増えない前提なので、Document内をそのまま線形検索する
	for (GraphNode& node : nodes) {
		if (node.id == id) {
			return &node;
		}
	}
	return nullptr;
}

const Engine::GraphNode* Engine::GraphDocument::FindNode(GraphID id) const {

	// const版も非const版と同じ検索順にして、取得結果の差を作らない
	for (const GraphNode& node : nodes) {
		if (node.id == id) {
			return &node;
		}
	}
	return nullptr;
}

Engine::GraphPin* Engine::GraphDocument::FindPin(GraphID id) {

	// PinはNode配下に保持しているため、全Nodeの入力/出力を順に確認する
	for (GraphNode& node : nodes) {
		for (GraphPin& pin : node.inputs) {
			if (pin.id == id) {
				return &pin;
			}
		}
		for (GraphPin& pin : node.outputs) {
			if (pin.id == id) {
				return &pin;
			}
		}
	}
	return nullptr;
}

const Engine::GraphPin* Engine::GraphDocument::FindPin(GraphID id) const {

	// LinkからPinを引く頻度が高いので、検索順は入力から出力で統一する
	for (const GraphNode& node : nodes) {
		for (const GraphPin& pin : node.inputs) {
			if (pin.id == id) {
				return &pin;
			}
		}
		for (const GraphPin& pin : node.outputs) {
			if (pin.id == id) {
				return &pin;
			}
		}
	}
	return nullptr;
}

Engine::GraphLink* Engine::GraphDocument::FindLink(GraphID id) {

	// Link IDから接続情報を取得する
	for (GraphLink& link : links) {
		if (link.id == id) {
			return &link;
		}
	}
	return nullptr;
}

const Engine::GraphLink* Engine::GraphDocument::FindLink(GraphID id) const {

	// const版も同じくDocument内のLinkを線形検索する
	for (const GraphLink& link : links) {
		if (link.id == id) {
			return &link;
		}
	}
	return nullptr;
}

Engine::GraphNode& Engine::GraphDocument::AddNode(GraphNode node) {

	// 外部からID未設定のNodeが渡された場合はDocument側でIDを採番する
	if (node.id == 0) {
		node.id = GenerateID();
	}

	// PinはNode IDとPin IDが揃っていないとLink作成時に辿れないため、追加時に補正する
	for (GraphPin& pin : node.inputs) {
		pin.nodeID = node.id;
		if (pin.id == 0) {
			pin.id = GenerateID();
		}
	}
	for (GraphPin& pin : node.outputs) {
		pin.nodeID = node.id;
		if (pin.id == 0) {
			pin.id = GenerateID();
		}
	}

	nodes.emplace_back(std::move(node));
	return nodes.back();
}

bool Engine::GraphDocument::RemoveNode(GraphID id) {

	const auto nodeIt = std::find_if(nodes.begin(), nodes.end(),
		[id](const GraphNode& node) { return node.id == id; });
	if (nodeIt == nodes.end()) {
		return false;
	}

	// ノード削除時は接続もまとめて消す
	links.erase(std::remove_if(links.begin(), links.end(),
		[&](const GraphLink& link) {
			const GraphPin* from = FindPin(link.fromPinID);
			const GraphPin* to = FindPin(link.toPinID);
			return (from && from->nodeID == id) || (to && to->nodeID == id);
		}), links.end());

	nodes.erase(nodeIt);
	return true;
}

bool Engine::GraphDocument::CanCreateLink(GraphID fromPinID, GraphID toPinID, std::string* reason) const {

	// Pinの存在確認。Drag中にNodeが消える可能性もあるため最初に確認する
	const GraphPin* from = FindPin(fromPinID);
	const GraphPin* to = FindPin(toPinID);
	if (!from || !to) {
		if (reason) {
			*reason = "Pin is not found.";
		}
		return false;
	}
	if (from->kind != GraphPinKind::Output || to->kind != GraphPinKind::Input) {
		if (reason) {
			*reason = "Link direction must be Output to Input.";
		}
		return false;
	}
	if (from->nodeID == to->nodeID) {
		if (reason) {
			*reason = "Same node pins can not be linked.";
		}
		return false;
	}
	if (from->valueType != GraphValueType::Unknown &&
		to->valueType != GraphValueType::Unknown &&
		from->valueType != to->valueType) {
		if (reason) {
			*reason = "Pin value type is different.";
		}
		return false;
	}
	if (!to->allowMultipleLinks && HasLinkToPin(to->id)) {
		if (reason) {
			*reason = "Input pin already has a link.";
		}
		return false;
	}
	return true;
}

Engine::GraphLink* Engine::GraphDocument::AddLink(GraphID fromPinID, GraphID toPinID) {

	// 実際の追加前に同じ判定を通して、View側以外から呼ばれても不正Linkを作らない
	if (!CanCreateLink(fromPinID, toPinID)) {
		return nullptr;
	}

	// Link IDだけは必ずDocument側で採番する
	GraphLink link{};
	link.id = GenerateID();
	link.fromPinID = fromPinID;
	link.toPinID = toPinID;
	links.emplace_back(link);
	return &links.back();
}

bool Engine::GraphDocument::RemoveLink(GraphID id) {

	// std::remove_ifの結果がendなら削除対象なし
	const auto it = std::remove_if(links.begin(), links.end(),
		[id](const GraphLink& link) { return link.id == id; });
	if (it == links.end()) {
		return false;
	}
	links.erase(it, links.end());
	return true;
}

bool Engine::GraphDocument::HasLinkToPin(GraphID pinID) const {

	// 入力Pinの多重接続禁止チェックで使用する
	for (const GraphLink& link : links) {
		if (link.toPinID == pinID) {
			return true;
		}
	}
	return false;
}

void Engine::GraphDocument::ClearValidationMessages() {

	// Validatorが毎回作り直すため、古い表示をNodeから消しておく
	for (GraphNode& node : nodes) {
		node.validationMessages.clear();
	}
}
