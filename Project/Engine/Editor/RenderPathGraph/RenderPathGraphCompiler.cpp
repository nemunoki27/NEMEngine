#include "RenderPathGraphCompiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/RenderPathGraph/RenderPathGraphNodeFactory.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphTypes.h>
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

	std::string FirstColorName(const nlohmann::json& targetSet) {

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

	nlohmann::json EnsureTargetSet(nlohmann::json value) {

		if (!value.is_object()) {
			value = nlohmann::json::object();
		}
		if (!value.contains("colors") || !value["colors"].is_array()) {
			value["colors"] = nlohmann::json::array();
		}
		return value;
	}

	void SetFirstColor(nlohmann::json& targetSet, const std::string& name) {

		if (name.empty()) {
			return;
		}
		targetSet = EnsureTargetSet(targetSet);
		if (targetSet["colors"].empty()) {
			targetSet["colors"].push_back(name);
		} else {
			targetSet["colors"][0] = name;
		}
	}

	void SetDepth(nlohmann::json& targetSet, const std::string& name) {

		if (name.empty()) {
			return;
		}
		targetSet = EnsureTargetSet(targetSet);
		targetSet["depth"] = name;
	}

	nlohmann::json PropertyOrRawTargetSet(const Engine::GraphNode& node, const char* key) {

		nlohmann::json value = node.properties.value(key, nlohmann::json::object());
		if (value.is_object()) {
			return value;
		}
		const nlohmann::json raw = node.properties.value("raw", nlohmann::json::object());
		return raw.is_object() ? raw.value(key, nlohmann::json::object()) : nlohmann::json::object();
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
			std::string depth = DepthName(PropertyOrRawTargetSet(*node, "dest"));
			if (depth.empty()) {
				depth = node->properties.value("depth", "");
			}
			return depth;
		}
		return {};
	}

	void ApplyInputResourceBindings(const Engine::GraphDocument& document,
		const Engine::GraphNode& node, nlohmann::json& raw) {

		if (const Engine::GraphPin* pin = FindInputPin(node, "SourceColor")) {
			if (const Engine::GraphLink* link = FindLinkToPin(document, pin->id)) {
				if (const Engine::GraphPin* from = document.FindPin(link->fromPinID)) {
					SetFirstColor(raw["source"], ResolveOutputResourceName(document, *from));
				}
			}
		}
		if (const Engine::GraphPin* pin = FindInputPin(node, "SourceDepth")) {
			if (const Engine::GraphLink* link = FindLinkToPin(document, pin->id)) {
				if (const Engine::GraphPin* from = document.FindPin(link->fromPinID)) {
					SetDepth(raw["source"], ResolveOutputResourceName(document, *from));
				}
			}
		}
	}

	void ApplyOutputResourceBindings(const Engine::GraphDocument& document,
		const Engine::GraphNode& node, nlohmann::json& raw) {

		if (const Engine::GraphPin* pin = FindOutputPin(node, "DestColor")) {
			if (document.HasLinkFromPin(pin->id)) {
				SetFirstColor(raw["dest"], ResolveOutputResourceName(document, *pin));
			}
		}
		if (const Engine::GraphPin* pin = FindOutputPin(node, "Depth")) {
			if (document.HasLinkFromPin(pin->id)) {
				SetDepth(raw["dest"], ResolveOutputResourceName(document, *pin));
			}
		}
		if (const Engine::GraphPin* pin = FindOutputPin(node, "View")) {
			if (document.HasLinkFromPin(pin->id) || node.type == Engine::RenderPathGraph::kBlit) {
				SetFirstColor(raw["dest"], "View");
			}
		}
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

	Engine::SceneRenderTargetDesc MakeTemporaryTargetDesc(const Engine::GraphNode& node) {

		Engine::SceneRenderTargetDesc desc{};
		desc.name = node.properties.value("name", "");
		desc.sizeMode = Engine::SceneRenderTargetSizeMode::ViewRelative;
		desc.widthScale = node.properties.value("widthScale", 1.0f);
		desc.heightScale = node.properties.value("heightScale", 1.0f);
		desc.fixedWidth = node.properties.value("fixedWidth", 0u);
		desc.fixedHeight = node.properties.value("fixedHeight", 0u);
		desc.createUAV = node.properties.value("createUAV", false);
		desc.withDepth = node.properties.value("withDepth", false);

		const std::string sizeMode = node.properties.value("sizeMode", "ViewRelative");
		if (const auto value = Engine::EnumAdapter<Engine::SceneRenderTargetSizeMode>::FromString(sizeMode)) {
			desc.sizeMode = *value;
		}
		const std::string format = node.properties.value("format", "RGBA32_FLOAT");
		if (const auto value = Engine::EnumAdapter<Engine::SceneRenderTargetFormat>::FromString(format)) {
			desc.colorFormat = *value;
		}

		Engine::SceneRenderTargetColorDesc color{};
		color.name = desc.name;
		color.format = desc.colorFormat;
		color.createUAV = desc.createUAV;
		desc.colors.emplace_back(std::move(color));
		return desc;
	}
}

bool Engine::RenderPathGraphCompiler::Compile(const GraphDocument& document, const SceneHeader& baseHeader,
	RenderPathGraphCompileResult& outResult, std::string* error) const {

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
		if (!CompileNode(document, *node, baseHeader, pass, error)) {
			return false;
		}
		compiled.emplace_back(std::move(pass));
	}

	outResult.renderTargets = CompileRenderTargets(document, baseHeader);
	outResult.passOrder = std::move(compiled);
	return true;
}

bool Engine::RenderPathGraphCompiler::Compile(const GraphDocument& document, const SceneHeader& baseHeader,
	std::vector<ScenePassDesc>& outPassOrder, std::string* error) const {

	RenderPathGraphCompileResult result{};
	if (!Compile(document, baseHeader, result, error)) {
		return false;
	}
	outPassOrder = std::move(result.passOrder);
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

std::vector<Engine::SceneRenderTargetDesc> Engine::RenderPathGraphCompiler::CompileRenderTargets(
	const GraphDocument& document, const SceneHeader& baseHeader) const {

	std::vector<SceneRenderTargetDesc> renderTargets = baseHeader.renderTargets;
	for (const GraphNode& node : document.nodes) {
		if (node.type != RenderPathGraph::kTemporaryTarget) {
			continue;
		}

		SceneRenderTargetDesc target = MakeTemporaryTargetDesc(node);
		if (target.name.empty()) {
			continue;
		}

		const auto it = std::find_if(renderTargets.begin(), renderTargets.end(),
			[&target](const SceneRenderTargetDesc& existing) { return existing.name == target.name; });
		if (it != renderTargets.end()) {
			*it = std::move(target);
		} else {
			renderTargets.emplace_back(std::move(target));
		}
	}
	return renderTargets;
}

bool Engine::RenderPathGraphCompiler::CompileNode(
	const GraphDocument& document, const GraphNode& node, const SceneHeader& baseHeader,
	ScenePassDesc& outPass, std::string* error) const {

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
	if (node.properties.contains("groupCountX")) {
		raw["groupCountX"] = node.properties["groupCountX"];
	}
	if (node.properties.contains("groupCountY")) {
		raw["groupCountY"] = node.properties["groupCountY"];
	}
	if (node.properties.contains("groupCountZ")) {
		raw["groupCountZ"] = node.properties["groupCountZ"];
	}
	if (node.properties.contains("subSceneSlot")) {
		raw["subSceneSlot"] = node.properties["subSceneSlot"];
	}
	if (node.properties.contains("target")) {
		SetFirstColor(raw["dest"], node.properties.value("target", ""));
	}
	if (node.properties.contains("clearColor")) {
		raw["clearColor"] = node.properties["clearColor"];
	}
	if (node.properties.contains("clearColorValue")) {
		raw["clearColorValue"] = node.properties["clearColorValue"];
	}
	if (node.properties.contains("clearDepth")) {
		raw["clearDepth"] = node.properties["clearDepth"];
	}
	if (node.properties.contains("clearDepthValue")) {
		raw["clearDepthValue"] = node.properties["clearDepthValue"];
	}
	if (node.properties.contains("clearStencil")) {
		raw["clearStencil"] = node.properties["clearStencil"];
	}
	if (node.properties.contains("clearStencilValue")) {
		raw["clearStencilValue"] = node.properties["clearStencilValue"];
	}

	// Pin接続はUI上の明示指定として、Property / rawより優先する
	ApplyInputResourceBindings(document, node, raw);
	ApplyOutputResourceBindings(document, node, raw);

	if (!PassFromRawJson(raw, outPass)) {
		if (error) {
			*error = "Failed to compile node. node=" + node.displayName;
		}
		return false;
	}
	return true;
}
