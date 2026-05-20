#include "RenderPathGraphCompiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/RenderPathGraph/RenderPathGraphNodeFactory.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

//============================================================================
//	RenderPathGraphCompiler classMethods
//============================================================================

namespace {

	const Engine::GraphPin* FindFlowInput(const Engine::GraphNode& node) {

		// Flow入力Pinを一つ探す
		for (const Engine::GraphPin& pin : node.inputs) {
			if (pin.valueType == Engine::GraphValueType::Flow) {
				return &pin;
			}
		}
		return nullptr;
	}

	const Engine::GraphPin* FindFlowOutput(const Engine::GraphNode& node) {

		// Flow出力Pinを一つ探す
		for (const Engine::GraphPin& pin : node.outputs) {
			if (pin.valueType == Engine::GraphValueType::Flow) {
				return &pin;
			}
		}
		return nullptr;
	}

	bool PassFromRawJson(const nlohmann::json& raw, Engine::ScenePassDesc& outPass) {

		// 既存のSceneHeaderシリアライズを通して、Pass単体を安全に復元する
		Engine::SceneHeader temp{};
		temp.passOrder.clear();

		nlohmann::json headerJson = nlohmann::json::object();
		headerJson["guid"] = Engine::ToString(Engine::UUID{});
		headerJson["name"] = "RenderPathCompile";
		headerJson["passOrder"] = nlohmann::json::array({ raw });

		if (!Engine::FromJson(headerJson, temp, nullptr) || temp.passOrder.empty()) {
			return false;
		}
		outPass = temp.passOrder.front();
		return true;
	}
}

bool Engine::RenderPathGraphCompiler::Compile(const GraphDocument& document, const SceneHeader& baseHeader,
	std::vector<ScenePassDesc>& outPassOrder, std::string* error) const {

	std::vector<const GraphNode*> orderedNodes = BuildExecutionOrder(document);
	if (orderedNodes.empty()) {
		if (error) {
			*error = "Executable RenderPath node is not found.";
		}
		return false;
	}

	std::vector<ScenePassDesc> compiled{};
	for (const GraphNode* node : orderedNodes) {
		// Resource Nodeなど、ScenePassにならないNodeはCompile対象外
		if (!node || !RenderPathGraphNodeFactory::IsScenePassNode(node->type)) {
			continue;
		}

		ScenePassDesc pass{};
		if (!CompileNode(*node, baseHeader, pass, error)) {
			return false;
		}
		compiled.emplace_back(std::move(pass));
	}

	outPassOrder = std::move(compiled);
	return true;
}

std::vector<const Engine::GraphNode*> Engine::RenderPathGraphCompiler::BuildExecutionOrder(
	const GraphDocument& document) const {

	std::unordered_map<GraphID, GraphID> flowOutToNode{};
	std::unordered_map<GraphID, GraphID> inputPinToNode{};
	std::unordered_map<GraphID, GraphID> nextNodeByNode{};
	std::unordered_map<GraphID, uint32_t> incomingCount{};

	// Flow PinをNode IDへ引けるようにMap化する
	for (const GraphNode& node : document.nodes) {
		if (const GraphPin* input = FindFlowInput(node)) {
			inputPinToNode[input->id] = node.id;
			incomingCount[node.id] = 0;
		}
		if (const GraphPin* output = FindFlowOutput(node)) {
			flowOutToNode[output->id] = node.id;
		}
	}

	// Flow Linkから実行順の隣接関係を作る
	for (const GraphLink& link : document.links) {
		const auto fromIt = flowOutToNode.find(link.fromPinID);
		const auto toIt = inputPinToNode.find(link.toPinID);
		if (fromIt == flowOutToNode.end() || toIt == inputPinToNode.end()) {
			continue;
		}
		nextNodeByNode[fromIt->second] = toIt->second;
		++incomingCount[toIt->second];
	}

	std::vector<const GraphNode*> ordered{};
	for (const auto& [nodeID, count] : incomingCount) {
		if (count != 0) {
			continue;
		}

		// 入力Flowが無いNodeを開始点として、次Nodeを辿る
		std::unordered_set<GraphID> visited{};
		GraphID current = nodeID;
		while (current != 0 && !visited.contains(current)) {
			visited.insert(current);
			if (const GraphNode* node = document.FindNode(current)) {
				ordered.emplace_back(node);
			}
			const auto nextIt = nextNodeByNode.find(current);
			current = nextIt != nextNodeByNode.end() ? nextIt->second : 0;
		}
	}

	// Flowがない古い/手編集グラフではpassIndex順に落とす
	if (ordered.empty()) {
		for (const GraphNode& node : document.nodes) {
			if (RenderPathGraphNodeFactory::IsScenePassNode(node.type)) {
				ordered.emplace_back(&node);
			}
		}
		std::sort(ordered.begin(), ordered.end(),
			[](const GraphNode* lhs, const GraphNode* rhs) {
				return lhs->properties.value("passIndex", 0u) < rhs->properties.value("passIndex", 0u);
			});
	}
	return ordered;
}

bool Engine::RenderPathGraphCompiler::CompileNode(
	const GraphNode& node, const SceneHeader& baseHeader, ScenePassDesc& outPass, std::string* error) const {

	(void)baseHeader;

	// 既存の未対応フィールドをできるだけ残すため、rawを起点にしてUI編集済みプロパティだけ上書きする
	nlohmann::json raw = node.properties.value("raw", nlohmann::json::object());
	if (!raw.is_object()) {
		raw = nlohmann::json::object();
	}
	raw["type"] = EnumAdapter<ScenePassType>::ToString(RenderPathGraphNodeFactory::ToPassType(node.type));
	raw["enabled"] = node.properties.value("enabled", node.enabled);

	if (node.properties.contains("queue")) {
		// UIで編集した値だけrawへ上書きする
		raw["queue"] = node.properties["queue"];
	}
	if (node.properties.contains("passName")) {
		raw["passName"] = node.properties["passName"];
	}
	if (node.properties.contains("material")) {
		raw["material"] = node.properties["material"];
	}
	if (node.properties.contains("source")) {
		raw["source"] = node.properties["source"];
	}
	if (node.properties.contains("dest")) {
		raw["dest"] = node.properties["dest"];
	}
	if (node.properties.contains("extraSources")) {
		raw["extraSources"] = node.properties["extraSources"];
	}
	if (node.properties.contains("dispatchMode")) {
		raw["dispatchMode"] = node.properties["dispatchMode"];
	}
	if (node.properties.contains("subSceneSlot")) {
		raw["subSceneSlot"] = node.properties["subSceneSlot"];
	}

	if (!PassFromRawJson(raw, outPass)) {
		if (error) {
			*error = "Failed to compile node. node=" + node.displayName;
		}
		return false;
	}
	return true;
}
