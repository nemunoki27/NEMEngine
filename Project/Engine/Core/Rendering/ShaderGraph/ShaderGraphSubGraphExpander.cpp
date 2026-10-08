#include "ShaderGraphSubGraphExpander.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <iterator>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace {

	using namespace Engine;

	// 展開後の接続先
	struct GraphEndpoint {

		Engine::UUID node{};
		uint32_t slot = 0;
	};

	// 呼出元と元のNodeから展開後の識別子を作る
	uint64_t MakeExpandedID(uint64_t instanceID, uint64_t sourceID, uint64_t discriminator) {

		uint64_t hash = 14695981039346656037ull;
		const auto append = [&](uint64_t value) {
			for (uint32_t byte = 0; byte < 8; ++byte) {
				hash ^= static_cast<uint8_t>(value >> (byte * 8));
				hash *= 1099511628211ull;
			}
		};
		append(instanceID);
		append(sourceID);
		append(discriminator);
		return hash != 0 ? hash : 1;
	}

	// 展開に失敗した参照元を診断へ残す
	void AddExpansionDiagnostic(std::vector<ShaderGraphDiagnostic>& diagnostics, Engine::UUID node, std::string message) {

		diagnostics.emplace_back(ShaderGraphDiagnostic{
			.severity = ShaderGraphDiagnosticSeverity::Error,
			.stage = ShaderGraphStage::Any,
			.node = node,
			.port = UINT32_MAX,
			.message = std::move(message),
		});
	}

	// 子Graphを展開して親の接続先へつなぐ
	bool ExpandSubGraphs(ShaderGraphAsset& graph, const ShaderGraphAssetResolver& resolver,
		std::vector<ShaderGraphDiagnostic>& diagnostics, std::unordered_set<AssetID>& resolving) {

		// 定数への置換前に公開値のIDを確認する
		if (!ShaderGraphIRBuilder::ValidatePublicIdentifiers(graph, diagnostics)) {
			return false;
		}

		while (true) {
			const auto instance = std::find_if(graph.nodes.begin(), graph.nodes.end(),
				[](const ShaderGraphNode& node) { return node.kind == ShaderGraphNodeKind::SubGraph; });
			if (instance == graph.nodes.end()) {
				return true;
			}

			const ShaderGraphNode subGraphNode = *instance;
			if (!resolver || !subGraphNode.subGraph) {
				AddExpansionDiagnostic(diagnostics, subGraphNode.id, "Sub Graphアセットが設定されていません");
				return false;
			}
			if (!resolving.insert(subGraphNode.subGraph).second) {
				AddExpansionDiagnostic(diagnostics, subGraphNode.id, "Sub Graphの参照が循環しています");
				return false;
			}

			// 子Graphの参照循環を先に解消する
			ShaderGraphAsset child{};
			if (!resolver(subGraphNode.subGraph, child)) {
				resolving.erase(subGraphNode.subGraph);
				AddExpansionDiagnostic(diagnostics, subGraphNode.id, "Sub Graphアセットを読み込めませんでした");
				return false;
			}
			if (child.domain != graph.domain) {
				resolving.erase(subGraphNode.subGraph);
				AddExpansionDiagnostic(diagnostics, subGraphNode.id, "異なるDomainのSub Graphは接続できません");
				return false;
			}
			if (!ExpandSubGraphs(child, resolver, diagnostics, resolving)) {
				resolving.erase(subGraphNode.subGraph);
				return false;
			}
			resolving.erase(subGraphNode.subGraph);

			// 公開値の順序を接続ピンへ対応付ける
			std::vector<const ShaderGraphParameter*> interfaceParameters;
			for (const ShaderGraphParameter& parameter : child.parameters) {
				if (parameter.exposed) {
					interfaceParameters.emplace_back(&parameter);
				}
			}
			std::vector<std::optional<GraphEndpoint>> instanceInputs(interfaceParameters.size());
			for (const ShaderGraphLink& link : graph.links) {
				if (link.inputNode == subGraphNode.id && link.inputSlot < instanceInputs.size()) {
					instanceInputs[link.inputSlot] = GraphEndpoint{
						.node = link.outputNode,
						.slot = link.outputSlot,
					};
				}
			}

			// 子のノードを呼出元ごとのIDへ置き換える
			std::unordered_map<uint64_t, GraphEndpoint> endpointMap;
			std::vector<ShaderGraphNode> expandedNodes;
			std::vector<ShaderGraphKeyword> expandedKeywords;
			for (const ShaderGraphKeyword& keyword : child.keywords) {
				ShaderGraphKeyword clone = keyword;
				clone.id = Engine::UUID{MakeExpandedID(subGraphNode.id.value, keyword.id.value, 6)};
				expandedKeywords.emplace_back(std::move(clone));
			}
			for (const ShaderGraphNode& source : child.nodes) {
				if (source.id == child.outputNode || source.id == child.vertexOutputNode) {
					continue;
				}
				if (source.kind == ShaderGraphNodeKind::Parameter) {
					const auto parameter = std::find_if(child.parameters.begin(), child.parameters.end(),
						[&](const ShaderGraphParameter& value) { return value.id == source.parameterID; });
					if (parameter == child.parameters.end()) {
						AddExpansionDiagnostic(diagnostics, subGraphNode.id, "Sub Graph内のParameterが見つかりません");
						return false;
					}
					const auto interfaceParameter =
						std::find(interfaceParameters.begin(), interfaceParameters.end(), &(*parameter));
					if (interfaceParameter != interfaceParameters.end()) {
						const size_t parameterIndex =
							static_cast<size_t>(std::distance(interfaceParameters.begin(), interfaceParameter));
						if (instanceInputs[parameterIndex]) {
							endpointMap[source.id.value] = *instanceInputs[parameterIndex];
							continue;
						}
					}

					ShaderGraphNode constant{
						.id = Engine::UUID{MakeExpandedID(subGraphNode.id.value, source.id.value, 1)},
						.kind = ShaderGraphNodeKind::Constant,
						.valueType = parameter->type,
						.value = parameter->defaultValue,
						.position = source.position,
						.previewExpanded = false,
					};
					endpointMap[source.id.value] = GraphEndpoint{
						.node = constant.id,
						.slot = 0,
					};
					expandedNodes.emplace_back(std::move(constant));
					continue;
				}

				ShaderGraphNode clone = source;
				clone.id = Engine::UUID{MakeExpandedID(subGraphNode.id.value, source.id.value, 2)};
				// Keywordの参照先も呼出元の定義へ置き換える
				if (source.kind == ShaderGraphNodeKind::Keyword) {
					const auto keyword = std::find_if(child.keywords.begin(), child.keywords.end(),
						[&](const ShaderGraphKeyword& value) { return value.id == source.keywordID; });
					if (keyword == child.keywords.end()) {
						AddExpansionDiagnostic(diagnostics, subGraphNode.id, "Sub Graph内のKeywordが見つかりません");
						return false;
					}
					clone.keywordID = Engine::UUID{MakeExpandedID(subGraphNode.id.value, source.keywordID.value, 6)};
				}
				for (ShaderGraphPort& port : clone.inputPorts) {
					port.id = Engine::UUID{MakeExpandedID(subGraphNode.id.value, port.id.value, 3)};
				}
				for (ShaderGraphPort& port : clone.outputPorts) {
					port.id = Engine::UUID{MakeExpandedID(subGraphNode.id.value, port.id.value, 4)};
				}
				endpointMap[source.id.value] = GraphEndpoint{
					.node = clone.id,
					.slot = 0,
				};
				expandedNodes.emplace_back(std::move(clone));
			}

			const auto resolveEndpoint = [&](Engine::UUID node, uint32_t slot) -> std::optional<GraphEndpoint> {
				const auto found = endpointMap.find(node.value);
				if (found == endpointMap.end()) {
					return std::nullopt;
				}
				GraphEndpoint endpoint = found->second;
				if (std::find_if(child.nodes.begin(), child.nodes.end(), [&](const ShaderGraphNode& value) {
						return value.id == node && value.kind == ShaderGraphNodeKind::Parameter;
					}) == child.nodes.end()) {
					endpoint.slot = slot;
				}
				return endpoint;
			};

			const auto childOutput = std::find_if(child.nodes.begin(), child.nodes.end(),
				[&](const ShaderGraphNode& node) { return node.id == child.outputNode; });
			if (childOutput == child.nodes.end()) {
				AddExpansionDiagnostic(diagnostics, subGraphNode.id, "Sub GraphのOutputノードが見つかりません");
				return false;
			}
			// 子の出力ピンと親の接続先を対応付ける
			std::vector<std::optional<GraphEndpoint>> outputs(GetShaderGraphInputCount(*childOutput));
			std::vector<ShaderGraphLink> expandedLinks;
			for (const ShaderGraphLink& sourceLink : child.links) {
				const std::optional<GraphEndpoint> source = resolveEndpoint(sourceLink.outputNode, sourceLink.outputSlot);
				if (!source) {
					continue;
				}
				if (sourceLink.inputNode == child.outputNode) {
					if (sourceLink.inputSlot < outputs.size()) {
						outputs[sourceLink.inputSlot] = source;
					}
					continue;
				}
				const auto destination = endpointMap.find(sourceLink.inputNode.value);
				if (destination == endpointMap.end()) {
					continue;
				}
				expandedLinks.emplace_back(ShaderGraphLink{
					.id = Engine::UUID{MakeExpandedID(subGraphNode.id.value, sourceLink.id.value, 5)},
					.outputNode = source->node,
					.outputSlot = source->slot,
					.inputNode = destination->second.node,
					.inputSlot = sourceLink.inputSlot,
				});
			}

			for (ShaderGraphLink& link : graph.links) {
				if (link.outputNode != subGraphNode.id) {
					continue;
				}
				if (link.outputSlot >= outputs.size() || !outputs[link.outputSlot]) {
					AddExpansionDiagnostic(diagnostics, subGraphNode.id, "Sub Graphの未接続出力が使用されています");
					return false;
				}
				link.outputNode = outputs[link.outputSlot]->node;
				link.outputSlot = outputs[link.outputSlot]->slot;
			}
			std::erase_if(graph.links, [&](const ShaderGraphLink& link) { return link.inputNode == subGraphNode.id; });
			graph.nodes.erase(std::find_if(graph.nodes.begin(), graph.nodes.end(),
				[&](const ShaderGraphNode& node) { return node.id == subGraphNode.id; }));
			graph.nodes.insert(graph.nodes.end(), std::make_move_iterator(expandedNodes.begin()),
				std::make_move_iterator(expandedNodes.end()));
			graph.links.insert(graph.links.end(), std::make_move_iterator(expandedLinks.begin()),
				std::make_move_iterator(expandedLinks.end()));
			graph.keywords.insert(graph.keywords.end(), std::make_move_iterator(expandedKeywords.begin()),
				std::make_move_iterator(expandedKeywords.end()));
		}
	}

} // namespace

bool Engine::ShaderGraphSubGraphExpander::Expand(
	ShaderGraphAsset& graph, const ShaderGraphAssetResolver& resolver, std::vector<ShaderGraphDiagnostic>& diagnostics) {

	// 呼出しごとに参照中の集合を作る
	std::unordered_set<AssetID> resolving;
	return ExpandSubGraphs(graph, resolver, diagnostics, resolving);
}
