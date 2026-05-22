#include "RenderPathGraphValidator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/RenderPathGraph/RenderPathGraphNodeFactory.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphTypes.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

//============================================================================
//	RenderPathGraphValidator classMethods
//============================================================================

namespace {

	using Severity = Engine::GraphValidationMessage::Severity;

	bool IsFlowPin(const Engine::GraphPin* pin) {

		// Flow Pinだけを実行順検証の対象にする
		return pin && pin->valueType == Engine::GraphValueType::Flow;
	}

	bool IsCompatibleValueType(Engine::GraphValueType from, Engine::GraphValueType to) {

		if (from == Engine::GraphValueType::Unknown || to == Engine::GraphValueType::Unknown) {
			return true;
		}
		if (from == to) {
			return true;
		}
		if (from == Engine::GraphValueType::Texture2DUAV && to == Engine::GraphValueType::Texture2D) {
			return true;
		}
		if (from == Engine::GraphValueType::RenderTarget &&
			(to == Engine::GraphValueType::Texture2D || to == Engine::GraphValueType::Texture2DUAV)) {
			return true;
		}
		return false;
	}

	std::string FirstColorName(const nlohmann::json& targetSet) {

		// TargetSet JSONから先頭Color名だけ取り出す
		if (!targetSet.is_object() || !targetSet.contains("colors") || !targetSet["colors"].is_array() ||
			targetSet["colors"].empty()) {
			return {};
		}
		return targetSet["colors"][0].is_string() ? targetSet["colors"][0].get<std::string>() : "";
	}

	std::string DepthName(const nlohmann::json& targetSet) {

		if (!targetSet.is_object() || !targetSet.contains("depth") || !targetSet["depth"].is_string()) {
			return {};
		}
		return targetSet["depth"].get<std::string>();
	}

	void CollectTemporaryTargetColors(const Engine::GraphDocument& document,
		std::unordered_set<std::string>& targets, std::unordered_set<std::string>* uavTargets) {

		for (const Engine::GraphNode& node : document.nodes) {
			if (node.type != Engine::RenderPathGraph::kTemporaryTarget) {
				continue;
			}
			const std::string name = node.properties.value("name", "");
			if (name.empty()) {
				continue;
			}
			targets.insert(name);
			if (uavTargets && node.properties.value("createUAV", false)) {
				uavTargets->insert(name);
			}
		}
	}

	std::unordered_set<std::string> CollectRenderTargetColors(
		const Engine::GraphDocument& document, const Engine::SceneHeader* sceneHeader) {

		// Passのsource / destが存在するTargetか確認するため、SceneHeaderから名前を集める
		std::unordered_set<std::string> result{};
		// 既定サーフェイス名はSceneHeaderに明示されないため、Validator側で許可する
		result.insert("SceneMain");
		result.insert("SceneFinal");
		result.insert("SceneColorFinal");
		result.insert("View");

		if (sceneHeader) {
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
		}
		CollectTemporaryTargetColors(document, result, nullptr);
		return result;
	}

	std::unordered_set<std::string> CollectUAVTargets(
		const Engine::GraphDocument& document, const Engine::SceneHeader* sceneHeader) {

		// Compute / PostProcessの出力先として使えるUAV対応Targetを集める
		std::unordered_set<std::string> result{};
		if (sceneHeader) {
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
		}
		std::unordered_set<std::string> renderTargets{};
		CollectTemporaryTargetColors(document, renderTargets, &result);
		return result;
	}

	const Engine::GraphPin* FindInputPin(const Engine::GraphNode& node, const char* name) {

		for (const Engine::GraphPin& pin : node.inputs) {
			if (pin.name == name) {
				return &pin;
			}
		}
		return nullptr;
	}

	const Engine::GraphPin* FindOutputPin(const Engine::GraphNode& node, const char* name) {

		for (const Engine::GraphPin& pin : node.outputs) {
			if (pin.name == name) {
				return &pin;
			}
		}
		return nullptr;
	}

	const Engine::GraphLink* FindLinkToPin(const Engine::GraphDocument& document, Engine::GraphID pinID) {

		for (const Engine::GraphLink& link : document.links) {
			if (link.toPinID == pinID) {
				return &link;
			}
		}
		return nullptr;
	}

	nlohmann::json PropertyOrRawTargetSet(const Engine::GraphNode& node, const char* key) {

		nlohmann::json value = node.properties.value(key, nlohmann::json::object());
		if (value.is_object()) {
			return value;
		}
		const nlohmann::json raw = node.properties.value("raw", nlohmann::json::object());
		return raw.is_object() ? raw.value(key, nlohmann::json::object()) : nlohmann::json::object();
	}

	bool HasLinkToView(const Engine::GraphDocument& document, Engine::GraphID pinID) {

		for (const Engine::GraphLink& link : document.links) {
			if (link.fromPinID != pinID) {
				continue;
			}
			const Engine::GraphPin* to = document.FindPin(link.toPinID);
			const Engine::GraphNode* node = to ? document.FindNode(to->nodeID) : nullptr;
			if (node && node->type == Engine::RenderPathGraph::kView) {
				return true;
			}
		}
		return false;
	}

	std::string ResolveOutputResourceName(const Engine::GraphDocument& document, const Engine::GraphPin& outputPin) {

		const Engine::GraphNode* node = document.FindNode(outputPin.nodeID);
		if (!node) {
			return {};
		}
		if (node->type == Engine::RenderPathGraph::kTemporaryTarget ||
			node->type == Engine::RenderPathGraph::kRenderTarget) {
			return node->properties.value("name", "");
		}
		if (node->type == Engine::RenderPathGraph::kReroute ||
			node->type == Engine::RenderPathGraph::kDepthReroute) {
			const Engine::GraphPin* input = FindInputPin(*node, "In");
			const Engine::GraphLink* link = input ? FindLinkToPin(document, input->id) : nullptr;
			const Engine::GraphPin* from = link ? document.FindPin(link->fromPinID) : nullptr;
			return from ? ResolveOutputResourceName(document, *from) : "";
		}
		if (outputPin.name == "View") {
			return "View";
		}
		if (outputPin.name == "DestColor") {
			const std::string dest = FirstColorName(PropertyOrRawTargetSet(*node, "dest"));
			return dest.empty() && HasLinkToView(document, outputPin.id) ? "View" : dest;
		}
		if (outputPin.name == "Depth") {
			return DepthName(PropertyOrRawTargetSet(*node, "dest"));
		}
		return {};
	}

	std::string ResolveLinkedInputResourceName(
		const Engine::GraphDocument& document, const Engine::GraphNode& node, const char* pinName) {

		const Engine::GraphPin* pin = FindInputPin(node, pinName);
		const Engine::GraphLink* link = pin ? FindLinkToPin(document, pin->id) : nullptr;
		const Engine::GraphPin* from = link ? document.FindPin(link->fromPinID) : nullptr;
		return from ? ResolveOutputResourceName(document, *from) : "";
	}

	std::vector<const Engine::GraphNode*> BuildFlowOrder(const Engine::GraphDocument& document) {

		std::unordered_map<Engine::GraphID, Engine::GraphID> nextNodeByNode{};
		std::unordered_map<Engine::GraphID, uint32_t> incomingFlowCount{};
		for (const Engine::GraphNode& node : document.nodes) {
			if (Engine::RenderPathGraphNodeFactory::IsScenePassNode(node.type) && FindInputPin(node, "In")) {
				incomingFlowCount[node.id] = 0;
			}
		}
		for (const Engine::GraphLink& link : document.links) {
			const Engine::GraphPin* from = document.FindPin(link.fromPinID);
			const Engine::GraphPin* to = document.FindPin(link.toPinID);
			if (!IsFlowPin(from) || !IsFlowPin(to)) {
				continue;
			}
			nextNodeByNode[from->nodeID] = to->nodeID;
			++incomingFlowCount[to->nodeID];
		}

		Engine::GraphID start = 0;
		for (const auto& [nodeID, count] : incomingFlowCount) {
			if (count == 0) {
				start = nodeID;
				break;
			}
		}

		std::vector<const Engine::GraphNode*> ordered{};
		std::unordered_set<Engine::GraphID> visited{};
		while (start != 0 && !visited.contains(start)) {
			visited.insert(start);
			if (const Engine::GraphNode* node = document.FindNode(start)) {
				ordered.emplace_back(node);
			}
			const auto nextIt = nextNodeByNode.find(start);
			start = nextIt != nextNodeByNode.end() ? nextIt->second : 0;
		}
		return ordered;
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
			result.Add(Severity::Error, 0, 0, "リンクが参照するピンが存在しません");
			continue;
		}
		if (from->kind != GraphPinKind::Output || to->kind != GraphPinKind::Input) {
			result.Add(Severity::Error, to->nodeID, to->id, "リンクの方向はOutput→Inputである必要があります");
		}
		if (!IsCompatibleValueType(from->valueType, to->valueType)) {
			result.Add(Severity::Error, to->nodeID, to->id, "ピンの型が一致しません");
		}
	}

	for (const GraphNode& node : document.nodes) {
		for (const GraphPin& pin : node.inputs) {
			if (!pin.allowMultipleLinks && document.CountLinksToPin(pin.id) > 1) {
				result.Add(Severity::Error, node.id, pin.id, "入力ピンに複数のリンクがあります");
			}
			// 必須入力Pinに未接続があればWarningを出す。ただしPropertyで値が設定済みの場合は抑制する
			if (pin.required && !document.HasLinkToPin(pin.id)) {
				bool suppressedByProperty = false;
				if (pin.name == "SourceColor") {
					suppressedByProperty = !FirstColorName(PropertyOrRawTargetSet(node, "source")).empty();
				} else if (pin.name == "SourceDepth") {
					suppressedByProperty = !DepthName(PropertyOrRawTargetSet(node, "source")).empty();
				} else if (pin.name == "DestColor") {
					suppressedByProperty = !FirstColorName(PropertyOrRawTargetSet(node, "dest")).empty();
				}
				if (!suppressedByProperty) {
					result.Add(Severity::Warning, node.id, pin.id, pin.name + " が接続されていません");
				}
			}
		}
		for (const GraphPin& pin : node.outputs) {
			if (pin.valueType == GraphValueType::Flow && document.CountLinksFromPin(pin.id) > 1) {
				result.Add(Severity::Error, node.id, pin.id, "フロー出力に複数のリンクがあります");
			}
		}
	}
}

void Engine::RenderPathGraphValidator::ValidateFlow(
	const GraphDocument& document, GraphValidationResult& result) const {

	std::unordered_map<GraphID, GraphID> nextNodeByNode{};
	std::unordered_map<GraphID, uint32_t> incomingFlowCount{};
	std::unordered_set<GraphID> flowNodeIDs{};
	uint32_t flowNodeCount = 0;

	// Flow入力を持つNodeを実行順Nodeとして数える
	for (const GraphNode& node : document.nodes) {
		for (const GraphPin& pin : node.inputs) {
			if (pin.valueType == GraphValueType::Flow) {
				incomingFlowCount[node.id] = 0;
				flowNodeIDs.insert(node.id);
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
		result.Add(Severity::Warning, 0, 0, "フローNodeが見つかりません");
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
		result.Add(Severity::Error, 0, 0, "フロー開始Nodeが見つかりません");
	}
	if (startCount > 1) {
		result.Add(Severity::Error, 0, 0, "フロー開始Nodeが複数あります");
	}

	// Flow linkの循環はApply時に順序を決められなくなるためErrorにする
	for (const auto& [startNode, _] : incomingFlowCount) {
		std::unordered_set<GraphID> visited{};
		GraphID current = startNode;
		while (current != 0) {
			if (visited.contains(current)) {
				result.Add(Severity::Error, current, 0, "フローに循環があります");
				break;
			}
			visited.insert(current);
			const auto nextIt = nextNodeByNode.find(current);
			current = nextIt != nextNodeByNode.end() ? nextIt->second : 0;
		}
	}

	if (startCount == 1) {
		GraphID start = 0;
		for (const auto& [nodeID, count] : incomingFlowCount) {
			if (count == 0) {
				start = nodeID;
				break;
			}
		}

		std::unordered_set<GraphID> reachable{};
		while (start != 0 && !reachable.contains(start)) {
			reachable.insert(start);
			const auto nextIt = nextNodeByNode.find(start);
			start = nextIt != nextNodeByNode.end() ? nextIt->second : 0;
		}

		for (GraphID nodeID : flowNodeIDs) {
			if (!reachable.contains(nodeID)) {
				result.Add(Severity::Warning, nodeID, 0, "到達できないフローNodeがあります");
			}
		}
	}
}

void Engine::RenderPathGraphValidator::ValidatePassProperties(
	const GraphDocument& document, const SceneHeader* sceneHeader, GraphValidationResult& result) const {

	const std::unordered_set<std::string> renderTargets = CollectRenderTargetColors(document, sceneHeader);
	const std::unordered_set<std::string> uavTargets = CollectUAVTargets(document, sceneHeader);

	// TemporaryTarget名の重複検出用
	std::unordered_set<std::string> seenTempNames{};
	static const std::unordered_set<std::string> kStandardTargets{ "SceneMain", "SceneFinal", "SceneColorFinal", "View" };

	// 最終表示につながるNodeがあるか確認する
	bool hasViewOutput = false;
	for (const GraphNode& node : document.nodes) {

		if (node.type == RenderPathGraph::kView &&
			!node.inputs.empty() && document.HasLinkToPin(node.inputs.front().id)) {
			hasViewOutput = true;
		}
		if (node.type == RenderPathGraph::kBlit) {
			hasViewOutput = true;
		}
		if (node.type == RenderPathGraph::kTemporaryTarget) {
			const std::string name = node.properties.value("name", "");
			const std::string format = node.properties.value("format", "RGBA32_FLOAT");
			const std::string sizeMode = node.properties.value("sizeMode", "ViewRelative");
			if (name.empty()) {
				result.Add(Severity::Error, node.id, 0, "TemporaryTarget名が空です");
			} else {
				if (!seenTempNames.insert(name).second) {
					result.Add(Severity::Error, node.id, 0, "TemporaryTarget名が重複しています : " + name);
				} else if (kStandardTargets.contains(name)) {
					result.Add(Severity::Warning, node.id, 0, "TemporaryTargetが既存のRenderTargetを上書きします : " + name);
				}
			}
			if (!EnumAdapter<SceneRenderTargetFormat>::FromString(format).has_value()) {
				result.Add(Severity::Error, node.id, 0, "RenderTargetのFormatが無効です");
			}
			if (!EnumAdapter<SceneRenderTargetSizeMode>::FromString(sizeMode).has_value()) {
				result.Add(Severity::Error, node.id, 0, "RenderTargetのSizeModeが無効です");
			}
			if (sizeMode == "Fixed") {
				const uint32_t width = node.properties.value("fixedWidth", 0u);
				const uint32_t height = node.properties.value("fixedHeight", 0u);
				if (width == 0 || height == 0) {
					result.Add(Severity::Error, node.id, 0, "Fixed RenderTargetのサイズが0です");
				}
			}
			continue;
		}
		if (!RenderPathGraphNodeFactory::IsScenePassNode(node.type)) {
			continue;
		}

		if (node.type == RenderPathGraph::kDraw) {
			// Draw Queueが空だと実描画で対象が曖昧になる
			const std::string queue = node.properties.value("queue", "");
			if (queue.empty()) {
				result.Add(Severity::Warning, node.id, 0, "DrawのQueueが空です");
			}
			if (FirstColorName(PropertyOrRawTargetSet(node, "dest")).empty()) {
				result.Add(Severity::Warning, node.id, 0, "DrawのDestが空です");
			}
		}

		if (node.type == RenderPathGraph::kDepthPrepass) {
			if (FirstColorName(PropertyOrRawTargetSet(node, "dest")).empty()) {
				result.Add(Severity::Warning, node.id, 0, "DepthPrepassのDestが空です");
			}
		}

		if (node.type == RenderPathGraph::kRenderScene) {
			if (FirstColorName(PropertyOrRawTargetSet(node, "dest")).empty()) {
				result.Add(Severity::Warning, node.id, 0, "RenderSceneのDestが空です");
			}
		}

		if (node.type == RenderPathGraph::kClear) {
			const std::string target = node.properties.value("target", "");
			if (target.empty()) {
				result.Add(Severity::Warning, node.id, 0, "ClearのTargetが空です");
			}
		}

		if (node.type == RenderPathGraph::kBlit) {
			const std::string dest = FirstColorName(PropertyOrRawTargetSet(node, "dest"));
			if (dest.empty()) {
				const GraphPin* viewPin = FindOutputPin(node, "View");
				if (!viewPin || !document.HasLinkFromPin(viewPin->id)) {
					result.Add(Severity::Warning, node.id, 0, "BlitのOutputTargetが空です");
				}
			}
		}

		if (node.type == RenderPathGraph::kRaytracing) {
			if (FirstColorName(PropertyOrRawTargetSet(node, "dest")).empty()) {
				result.Add(Severity::Warning, node.id, 0, "RaytracingのDestが空です");
			}
		}

		if (node.type == RenderPathGraph::kPostProcess || node.type == RenderPathGraph::kCompute) {
			// PostProcess / Computeは同一Resourceへのin-place処理を禁止する
			std::string source = ResolveLinkedInputResourceName(document, node, "SourceColor");
			if (source.empty()) {
				source = FirstColorName(PropertyOrRawTargetSet(node, "source"));
			}
			std::string dest = FirstColorName(PropertyOrRawTargetSet(node, "dest"));
			if (const GraphPin* output = FindOutputPin(node, "DestColor");
				output && document.HasLinkFromPin(output->id)) {
				dest = ResolveOutputResourceName(document, *output);
			}
			if (!source.empty() && !dest.empty() && source == dest) {
				result.Add(Severity::Error, node.id, 0, "PostProcess / Compute のSource/Destが同じです");
			}
			if (!dest.empty() && renderTargets.contains(dest) && !uavTargets.contains(dest)) {
				result.Add(Severity::Warning, node.id, 0, "DestターゲットにUAVフラグがありません");
			}
			if (!dest.empty() && !renderTargets.empty() && !renderTargets.contains(dest)) {
				result.Add(Severity::Warning, node.id, 0, "Destターゲットがシーンに存在しません");
			}
			if (dest.empty()) {
				result.Add(Severity::Warning, node.id, 0,
					node.type == RenderPathGraph::kPostProcess ? "PostProcessのDestが空です" : "ComputeのDestが空です");
			}
		}

		if ((node.type == RenderPathGraph::kPostProcess ||
			node.type == RenderPathGraph::kCompute ||
			node.type == RenderPathGraph::kBlit ||
			node.type == RenderPathGraph::kRaytracing) &&
			node.properties.value("material", "").empty()) {
			result.Add(Severity::Warning, node.id, 0, "Materialが設定されていません");
		}
	}

	if (!hasViewOutput) {
		result.Add(Severity::Warning, 0, 0, "View出力またはBlitパスが見つかりません");
	}

	// Flow順にPassを辿り、Viewへ到達しない場合はWarningを出す
	{
		const std::vector<const GraphNode*> flowOrder = BuildFlowOrder(document);
		if (!flowOrder.empty()) {
			bool chainReachesView = false;
			for (const GraphNode* n : flowOrder) {
				if (!n) {
					continue;
				}
				if (n->type == RenderPathGraph::kBlit) {
					chainReachesView = true;
					break;
				}
				if (n->type == RenderPathGraph::kView &&
					!n->inputs.empty() && document.HasLinkToPin(n->inputs.front().id)) {
					chainReachesView = true;
					break;
				}
			}
			if (!chainReachesView) {
				for (const GraphNode* n : flowOrder) {
					if (n && RenderPathGraphNodeFactory::IsScenePassNode(n->type)) {
						result.Add(Severity::Warning, n->id, 0, "PassがView出力に到達しません");
					}
				}
			}
		}
	}

	// 標準ターゲット (SceneMain等) はエンジンがフレーム開始前に初期化するため、書き込み済みとして扱う
	std::unordered_set<std::string> written(kStandardTargets.begin(), kStandardTargets.end());
	for (const GraphNode* node : BuildFlowOrder(document)) {
		if (!node || !RenderPathGraphNodeFactory::IsScenePassNode(node->type)) {
			continue;
		}

		std::string source = ResolveLinkedInputResourceName(document, *node, "SourceColor");
		if (source.empty()) {
			source = FirstColorName(PropertyOrRawTargetSet(*node, "source"));
		}
		std::string sourceDepth = ResolveLinkedInputResourceName(document, *node, "SourceDepth");
		if (sourceDepth.empty()) {
			sourceDepth = DepthName(PropertyOrRawTargetSet(*node, "source"));
		}
		if (!source.empty() && renderTargets.contains(source) && !written.contains(source)) {
			result.Add(Severity::Error, node->id, 0, "Resourceが書き込み前に読み込まれています");
		}
		if (!sourceDepth.empty() && renderTargets.contains(sourceDepth) && !written.contains(sourceDepth)) {
			result.Add(Severity::Error, node->id, 0, "DepthResourceが書き込み前に読み込まれています");
		}

		std::string dest = FirstColorName(PropertyOrRawTargetSet(*node, "dest"));
		if (const GraphPin* output = FindOutputPin(*node, "DestColor");
			output && document.HasLinkFromPin(output->id)) {
			dest = ResolveOutputResourceName(document, *output);
		}
		if (!source.empty() && !dest.empty() && source == dest) {
			result.Add(Severity::Error, node->id, 0, "同一パスでResourceの読み書きが発生しています");
		}
		if (!dest.empty()) {
			written.insert(dest);
		}
		if (const std::string destDepth = DepthName(PropertyOrRawTargetSet(*node, "dest")); !destDepth.empty()) {
			written.insert(destDepth);
		}
	}
}
