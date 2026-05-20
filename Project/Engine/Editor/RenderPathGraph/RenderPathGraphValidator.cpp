#include "RenderPathGraphValidator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/RenderPathGraph/RenderPathGraphNodeFactory.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphTypes.h>

// c++
#include <functional>
#include <unordered_map>
#include <unordered_set>

//============================================================================
//	RenderPathGraphValidator classMethods
//============================================================================

namespace {

	using Severity = Engine::GraphValidationMessage::Severity;

	bool IsFlowPin(const Engine::GraphPin* pin) {

		// Flow Pinだけを実行順検証の対象にする
		return pin && pin->valueType == Engine::GraphValueType::Flow;
	}

	std::string FirstColorName(const nlohmann::json& targetSet) {

		// TargetSet JSONから先頭Color名だけ取り出す
		if (!targetSet.is_object() || !targetSet.contains("colors") || !targetSet["colors"].is_array() ||
			targetSet["colors"].empty()) {
			return {};
		}
		return targetSet["colors"][0].is_string() ? targetSet["colors"][0].get<std::string>() : "";
	}

	std::unordered_set<std::string> CollectRenderTargetColors(const Engine::SceneHeader* sceneHeader) {

		// Passのsource / destが存在するTargetか確認するため、SceneHeaderから名前を集める
		std::unordered_set<std::string> result{};
		if (!sceneHeader) {
			return result;
		}

		// 既定サーフェイス名はSceneHeaderに明示されないため、Validator側で許可する
		result.insert("SceneMain");
		result.insert("SceneFinal");
		result.insert("SceneColorFinal");
		result.insert("View");

		for (const Engine::SceneRenderTargetDesc& target : sceneHeader->renderTargets) {
			if (!target.name.empty()) {
				result.insert(target.name);
			}
			for (const Engine::SceneRenderTargetColorDesc& color : target.colors) {
				if (!color.name.empty()) {
					result.insert(color.name);
				}
			}
		}
		return result;
	}

	std::unordered_set<std::string> CollectUAVTargets(const Engine::SceneHeader* sceneHeader) {

		// Compute / PostProcessの出力先として使えるUAV対応Targetを集める
		std::unordered_set<std::string> result{};
		if (!sceneHeader) {
			return result;
		}
		for (const Engine::SceneRenderTargetDesc& target : sceneHeader->renderTargets) {
			if (target.createUAV && !target.name.empty()) {
				result.insert(target.name);
			}
			for (const Engine::SceneRenderTargetColorDesc& color : target.colors) {
				if (color.createUAV && !color.name.empty()) {
					result.insert(color.name);
				}
			}
		}
		return result;
	}

	void AddNodeMessage(Engine::GraphDocument& document,
		const Engine::GraphValidationMessage& message) {

		// Graph全体の結果とは別に、Node上へ直接出すメッセージも保持する
		if (Engine::GraphNode* node = document.FindNode(message.nodeID)) {
			node->validationMessages.emplace_back(message.message);
		}
	}
}

Engine::GraphValidationResult Engine::RenderPathGraphValidator::Validate(
	GraphDocument& document, const SceneHeader* sceneHeader) const {

	document.ClearValidationMessages();

	// Link / Flow / Pass内容を分けて検証し、表示側でまとめて扱う
	GraphValidationResult result{};
	ValidateLinks(document, result);
	ValidateFlow(document, result);
	ValidatePassProperties(document, sceneHeader, result);

	for (const GraphValidationMessage& message : result.messages) {
		if (message.nodeID != 0) {
			// Nodeに紐づく警告はNode本体にも表示する
			AddNodeMessage(document, message);
		}
	}
	return result;
}

void Engine::RenderPathGraphValidator::ValidateLinks(
	const GraphDocument& document, GraphValidationResult& result) const {

	for (const GraphLink& link : document.links) {

		// Linkが参照しているPinの存在と型を確認する
		const GraphPin* from = document.FindPin(link.fromPinID);
		const GraphPin* to = document.FindPin(link.toPinID);
		if (!from || !to) {
			result.Add(Severity::Error, 0, 0, "Link references missing pin.");
			continue;
		}
		if (from->kind != GraphPinKind::Output || to->kind != GraphPinKind::Input) {
			result.Add(Severity::Error, to->nodeID, to->id, "Link direction must be Output to Input.");
		}
		if (from->valueType != GraphValueType::Unknown &&
			to->valueType != GraphValueType::Unknown &&
			from->valueType != to->valueType) {
			result.Add(Severity::Error, to->nodeID, to->id, "Pin value type is different.");
		}
	}

	for (const GraphNode& node : document.nodes) {
		for (const GraphPin& pin : node.inputs) {
			// 必須入力Pinに未接続があればWarningを出す
			if (pin.required && !document.HasLinkToPin(pin.id)) {
				result.Add(Severity::Warning, node.id, pin.id, pin.name + " input is not linked.");
			}
		}
	}
}

void Engine::RenderPathGraphValidator::ValidateFlow(
	const GraphDocument& document, GraphValidationResult& result) const {

	std::unordered_map<GraphID, GraphID> nextNodeByNode{};
	std::unordered_map<GraphID, uint32_t> incomingFlowCount{};
	uint32_t flowNodeCount = 0;

	// Flow入力を持つNodeを実行順Nodeとして数える
	for (const GraphNode& node : document.nodes) {
		for (const GraphPin& pin : node.inputs) {
			if (pin.valueType == GraphValueType::Flow) {
				incomingFlowCount[node.id] = 0;
				++flowNodeCount;
				break;
			}
		}
	}

	// Flow Linkから次Nodeと入力数を集計する
	for (const GraphLink& link : document.links) {
		const GraphPin* from = document.FindPin(link.fromPinID);
		const GraphPin* to = document.FindPin(link.toPinID);
		if (!IsFlowPin(from) || !IsFlowPin(to)) {
			continue;
		}
		nextNodeByNode[from->nodeID] = to->nodeID;
		++incomingFlowCount[to->nodeID];
	}

	if (flowNodeCount == 0) {
		result.Add(Severity::Warning, 0, 0, "Flow node is not found.");
		return;
	}

	uint32_t startCount = 0;
	for (const auto& [nodeID, count] : incomingFlowCount) {
		(void)nodeID;
		if (count == 0) {
			++startCount;
		}
	}
	if (startCount == 0) {
		result.Add(Severity::Error, 0, 0, "Flow start node is not found.");
	}

	// Flow linkの循環はApply時に順序を決められなくなるためErrorにする
	for (const auto& [startNode, _] : incomingFlowCount) {
		std::unordered_set<GraphID> visited{};
		GraphID current = startNode;
		while (current != 0) {
			if (visited.contains(current)) {
				result.Add(Severity::Error, current, 0, "Flow has cycle.");
				break;
			}
			visited.insert(current);
			const auto nextIt = nextNodeByNode.find(current);
			current = nextIt != nextNodeByNode.end() ? nextIt->second : 0;
		}
	}
}

void Engine::RenderPathGraphValidator::ValidatePassProperties(
	const GraphDocument& document, const SceneHeader* sceneHeader, GraphValidationResult& result) const {

	const std::unordered_set<std::string> renderTargets = CollectRenderTargetColors(sceneHeader);
	const std::unordered_set<std::string> uavTargets = CollectUAVTargets(sceneHeader);

	// 最終表示につながるNodeがあるか確認する
	bool hasViewOutput = false;
	for (const GraphNode& node : document.nodes) {

		if (node.type == RenderPathGraph::kView || node.type == RenderPathGraph::kBlit) {
			hasViewOutput = true;
		}
		if (!RenderPathGraphNodeFactory::IsScenePassNode(node.type)) {
			continue;
		}

		if (node.type == RenderPathGraph::kDraw) {
			// Draw Queueが空だと実描画で対象が曖昧になる
			const std::string queue = node.properties.value("queue", "");
			if (queue.empty()) {
				result.Add(Severity::Warning, node.id, 0, "Draw queue is empty.");
			}
		}

		if (node.type == RenderPathGraph::kPostProcess || node.type == RenderPathGraph::kCompute) {
			// PostProcess / Computeは同一Resourceへのin-place処理を禁止する
			const std::string source = FirstColorName(node.properties.value("source", nlohmann::json::object()));
			const std::string dest = FirstColorName(node.properties.value("dest", nlohmann::json::object()));
			if (!source.empty() && !dest.empty() && source == dest) {
				result.Add(Severity::Error, node.id, 0, "PostProcess / Compute source and dest are same.");
			}
			if (!dest.empty() && renderTargets.contains(dest) && !uavTargets.contains(dest)) {
				result.Add(Severity::Warning, node.id, 0, "Dest target is not marked as UAV.");
			}
			if (!dest.empty() && !renderTargets.empty() && !renderTargets.contains(dest)) {
				result.Add(Severity::Warning, node.id, 0, "Dest target is not found in scene renderTargets.");
			}
		}

		if ((node.type == RenderPathGraph::kPostProcess ||
			node.type == RenderPathGraph::kCompute ||
			node.type == RenderPathGraph::kBlit ||
			node.type == RenderPathGraph::kRaytracing) &&
			node.properties.value("material", "").empty()) {
			result.Add(Severity::Warning, node.id, 0, "Material is empty.");
		}
	}

	if (!hasViewOutput) {
		result.Add(Severity::Warning, 0, 0, "View output or Blit pass is not found.");
	}
}
