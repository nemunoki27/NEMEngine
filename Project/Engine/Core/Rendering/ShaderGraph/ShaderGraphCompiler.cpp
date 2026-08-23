#include "ShaderGraphCompiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphBindingNames.h>

// c++
#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

	using namespace Engine;

	struct GraphExpression {

		ShaderGraphValueType type = ShaderGraphValueType::Invalid;
		std::string code;
	};

	struct InputKey {

		uint64_t node = 0;
		uint32_t slot = 0;

		bool operator==(const InputKey&) const = default;
	};

	struct InputKeyHasher {

		size_t operator()(const InputKey& key) const noexcept {
			return std::hash<uint64_t>{}(
				key.node ^ (static_cast<uint64_t>(key.slot) << 32));
		}
	};

	struct NodeOutputKey {

		uint64_t node = 0;
		uint32_t slot = 0;

		bool operator==(const NodeOutputKey&) const = default;
	};

	struct NodeOutputKeyHasher {

		size_t operator()(const NodeOutputKey& key) const noexcept {

			const size_t nodeHash = std::hash<uint64_t>{}(key.node);
			const size_t slotHash = std::hash<uint32_t>{}(key.slot);
			return nodeHash ^ (slotHash + 0x9e3779b97f4a7c15ull +
				(nodeHash << 6) + (nodeHash >> 2));
		}
	};

	struct GraphEndpoint {

		Engine::UUID node{};
		uint32_t slot = 0;
	};

	uint64_t MakeExpandedID(
		uint64_t instanceID, uint64_t sourceID,
		uint64_t discriminator) {

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

	void AddExpansionDiagnostic(
		std::vector<ShaderGraphDiagnostic>& diagnostics,
		Engine::UUID node, std::string message) {

		diagnostics.emplace_back(ShaderGraphDiagnostic{
			.severity = ShaderGraphDiagnosticSeverity::Error,
			.stage = ShaderGraphStage::Any,
			.node = node,
			.port = UINT32_MAX,
			.message = std::move(message),
			});
	}

	bool ExpandSubGraphs(
		ShaderGraphAsset& graph,
		const ShaderGraphAssetResolver& resolver,
		std::vector<ShaderGraphDiagnostic>& diagnostics,
		std::unordered_set<AssetID>& resolving) {

		while (true) {
			const auto instance = std::find_if(
				graph.nodes.begin(), graph.nodes.end(),
				[](const ShaderGraphNode& node) {
					return node.kind == ShaderGraphNodeKind::SubGraph;
				});
			if (instance == graph.nodes.end()) {
				return true;
			}

			const ShaderGraphNode subGraphNode = *instance;
			if (!resolver || !subGraphNode.subGraph) {
				AddExpansionDiagnostic(diagnostics, subGraphNode.id,
					"Sub Graphアセットが設定されていません");
				return false;
			}
			if (!resolving.insert(subGraphNode.subGraph).second) {
				AddExpansionDiagnostic(diagnostics, subGraphNode.id,
					"Sub Graphの参照が循環しています");
				return false;
			}

			ShaderGraphAsset child{};
			if (!resolver(subGraphNode.subGraph, child)) {
				resolving.erase(subGraphNode.subGraph);
				AddExpansionDiagnostic(diagnostics, subGraphNode.id,
					"Sub Graphアセットを読み込めませんでした");
				return false;
			}
			if (child.domain != graph.domain) {
				resolving.erase(subGraphNode.subGraph);
				AddExpansionDiagnostic(diagnostics, subGraphNode.id,
					"異なるDomainのSub Graphは接続できません");
				return false;
			}
			if (!ExpandSubGraphs(
				child, resolver, diagnostics, resolving)) {
				resolving.erase(subGraphNode.subGraph);
				return false;
			}
			resolving.erase(subGraphNode.subGraph);

			std::vector<const ShaderGraphParameter*> interfaceParameters;
			for (const ShaderGraphParameter& parameter : child.parameters) {
				if (parameter.exposed) {
					interfaceParameters.emplace_back(&parameter);
				}
			}
			std::vector<std::optional<GraphEndpoint>> instanceInputs(
				interfaceParameters.size());
			for (const ShaderGraphLink& link : graph.links) {
				if (link.inputNode == subGraphNode.id &&
					link.inputSlot < instanceInputs.size()) {
					instanceInputs[link.inputSlot] = GraphEndpoint{
						.node = link.outputNode,
						.slot = link.outputSlot,
					};
				}
			}

			std::unordered_map<uint64_t, GraphEndpoint> endpointMap;
			std::vector<ShaderGraphNode> expandedNodes;
			for (const ShaderGraphNode& source : child.nodes) {
				if (source.id == child.outputNode ||
					source.id == child.vertexOutputNode) {
					continue;
				}
				if (source.kind == ShaderGraphNodeKind::Parameter) {
					const auto parameter = std::find_if(
						child.parameters.begin(), child.parameters.end(),
						[&](const ShaderGraphParameter& value) {
							return value.id == source.parameterID;
						});
					if (parameter == child.parameters.end()) {
						AddExpansionDiagnostic(diagnostics, subGraphNode.id,
							"Sub Graph内のParameterが見つかりません");
						return false;
					}
					const auto interfaceParameter = std::find(
						interfaceParameters.begin(), interfaceParameters.end(),
						&(*parameter));
					if (interfaceParameter != interfaceParameters.end() &&
						instanceInputs[static_cast<size_t>(std::distance(
							interfaceParameters.begin(), interfaceParameter))]) {
						const size_t parameterIndex = static_cast<size_t>(
							std::distance(interfaceParameters.begin(), interfaceParameter));
						endpointMap[source.id.value] =
							*instanceInputs[parameterIndex];
						continue;
					}

					ShaderGraphNode constant{
						.id = Engine::UUID{ MakeExpandedID(
							subGraphNode.id.value, source.id.value, 1) },
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
				clone.id = Engine::UUID{ MakeExpandedID(
					subGraphNode.id.value, source.id.value, 2) };
				for (ShaderGraphPort& port : clone.inputPorts) {
					port.id = Engine::UUID{ MakeExpandedID(
						subGraphNode.id.value, port.id.value, 3) };
				}
				for (ShaderGraphPort& port : clone.outputPorts) {
					port.id = Engine::UUID{ MakeExpandedID(
						subGraphNode.id.value, port.id.value, 4) };
				}
				endpointMap[source.id.value] = GraphEndpoint{
					.node = clone.id,
					.slot = 0,
				};
				expandedNodes.emplace_back(std::move(clone));
			}

			const auto resolveEndpoint = [&](Engine::UUID node, uint32_t slot)
				-> std::optional<GraphEndpoint> {
				const auto found = endpointMap.find(node.value);
				if (found == endpointMap.end()) {
					return std::nullopt;
				}
				GraphEndpoint endpoint = found->second;
				if (std::find_if(child.nodes.begin(), child.nodes.end(),
					[&](const ShaderGraphNode& value) {
						return value.id == node &&
							value.kind == ShaderGraphNodeKind::Parameter;
					}) == child.nodes.end()) {
					endpoint.slot = slot;
				}
				return endpoint;
				};

			const auto childOutput = std::find_if(
				child.nodes.begin(), child.nodes.end(),
				[&](const ShaderGraphNode& node) {
					return node.id == child.outputNode;
				});
			if (childOutput == child.nodes.end()) {
				AddExpansionDiagnostic(diagnostics, subGraphNode.id,
					"Sub GraphのOutputノードが見つかりません");
				return false;
			}
			std::vector<std::optional<GraphEndpoint>> outputs(
				GetShaderGraphInputCount(*childOutput));
			std::vector<ShaderGraphLink> expandedLinks;
			for (const ShaderGraphLink& sourceLink : child.links) {
				const std::optional<GraphEndpoint> source =
					resolveEndpoint(sourceLink.outputNode, sourceLink.outputSlot);
				if (!source) {
					continue;
				}
				if (sourceLink.inputNode == child.outputNode) {
					if (sourceLink.inputSlot < outputs.size()) {
						outputs[sourceLink.inputSlot] = source;
					}
					continue;
				}
				const auto destination = endpointMap.find(
					sourceLink.inputNode.value);
				if (destination == endpointMap.end()) {
					continue;
				}
				expandedLinks.emplace_back(ShaderGraphLink{
					.id = Engine::UUID{ MakeExpandedID(
						subGraphNode.id.value, sourceLink.id.value, 5) },
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
				if (link.outputSlot >= outputs.size() ||
					!outputs[link.outputSlot]) {
					AddExpansionDiagnostic(diagnostics, subGraphNode.id,
						"Sub Graphの未接続出力が使用されています");
					return false;
				}
				link.outputNode = outputs[link.outputSlot]->node;
				link.outputSlot = outputs[link.outputSlot]->slot;
			}
			std::erase_if(graph.links,
				[&](const ShaderGraphLink& link) {
					return link.inputNode == subGraphNode.id;
				});
			graph.nodes.erase(std::find_if(
				graph.nodes.begin(), graph.nodes.end(),
				[&](const ShaderGraphNode& node) {
					return node.id == subGraphNode.id;
				}));
			graph.nodes.insert(graph.nodes.end(),
				std::make_move_iterator(expandedNodes.begin()),
				std::make_move_iterator(expandedNodes.end()));
			graph.links.insert(graph.links.end(),
				std::make_move_iterator(expandedLinks.begin()),
				std::make_move_iterator(expandedLinks.end()));
		}
	}

	std::string FormatFloat(float value) {

		std::ostringstream stream;
		stream << std::setprecision(9) << value;
		std::string result = stream.str();
		if (result.find_first_of(".eE") == std::string::npos) {
			result += ".0";
		}
		result += "f";
		return result;
	}

	uint32_t ComponentCount(ShaderGraphValueType type) {

		switch (type) {
		case ShaderGraphValueType::Float: return 1;
		case ShaderGraphValueType::Float2: return 2;
		case ShaderGraphValueType::Float3: return 3;
		case ShaderGraphValueType::Float4:
		case ShaderGraphValueType::Color: return 4;
		case ShaderGraphValueType::Boolean:
		case ShaderGraphValueType::Integer: return 1;
		default: return 0;
		}
	}

	bool IsNumeric(ShaderGraphValueType type) {

		return ComponentCount(type) > 0;
	}

	std::string HLSLType(ShaderGraphValueType type) {

		switch (type) {
		case ShaderGraphValueType::Float: return "float";
		case ShaderGraphValueType::Float2: return "float2";
		case ShaderGraphValueType::Float3: return "float3";
		case ShaderGraphValueType::Float4:
		case ShaderGraphValueType::Color: return "float4";
		case ShaderGraphValueType::Texture2D: return "uint";
		case ShaderGraphValueType::SamplerState: return "SamplerState";
		case ShaderGraphValueType::Boolean: return "uint";
		case ShaderGraphValueType::Integer: return "int";
		case ShaderGraphValueType::Matrix4: return "float4x4";
		default: return "float";
		}
	}

	std::string MakeIdentifier(
		std::string_view name, Engine::UUID id) {

		std::string result = "p_";
		result.reserve(name.size() + 12);
		for (const char character : name) {
			const bool valid =
				('a' <= character && character <= 'z') ||
				('A' <= character && character <= 'Z') ||
				('0' <= character && character <= '9') ||
				character == '_';
			result.push_back(valid ? character : '_');
		}
		const std::string idText = ToString(id);
		result += "_";
		result += idText.substr(8);
		return result;
	}

	std::string MakeNodeVariable(
		std::string_view prefix, Engine::UUID id) {

		return std::string(prefix) + "_" +
			ToString(id);
	}

	std::string MakeLiteral(
		const MaterialParameterValue& value,
		ShaderGraphValueType type) {

		auto component = [&](uint32_t index, float fallback) {
			return std::visit([&](const auto& current) -> float {
				using ValueType = std::decay_t<decltype(current)>;
				if constexpr (std::is_same_v<ValueType, float>) {
					return index == 0 ? current : fallback;
				} else if constexpr (std::is_same_v<ValueType, Vector2>) {
					return index == 0 ? current.x :
						(index == 1 ? current.y : fallback);
				} else if constexpr (std::is_same_v<ValueType, Vector3>) {
					return index == 0 ? current.x :
						(index == 1 ? current.y :
							(index == 2 ? current.z : fallback));
				} else if constexpr (std::is_same_v<ValueType, Vector4>) {
					return index == 0 ? current.x :
						(index == 1 ? current.y :
							(index == 2 ? current.z : current.w));
				} else if constexpr (std::is_same_v<ValueType, Color4>) {
					return index == 0 ? current.r :
						(index == 1 ? current.g :
							(index == 2 ? current.b : current.a));
				} else if constexpr (std::is_same_v<ValueType, int32_t> ||
					std::is_same_v<ValueType, uint32_t>) {

					return index == 0 ? static_cast<float>(current) : fallback;
				} else if constexpr (std::is_same_v<ValueType, bool>) {
					return index == 0 && current ? 1.0f : fallback;
				} else {
					return fallback;
				}
				}, value.value);
		};

		if (type == ShaderGraphValueType::Texture2D) {
			return "kNoTexture";
		}
		if (type == ShaderGraphValueType::Boolean) {
			return component(0, 0.0f) != 0.0f ? "1u" : "0u";
		}
		if (type == ShaderGraphValueType::Integer) {
			return std::to_string(static_cast<int32_t>(component(0, 0.0f)));
		}
		if (type == ShaderGraphValueType::Matrix4) {
			return "float4x4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f)";
		}
		const uint32_t count = ComponentCount(type);
		if (count <= 1) {
			return FormatFloat(component(0, 0.0f));
		}

		std::string result = HLSLType(type) + "(";
		for (uint32_t index = 0; index < count; ++index) {
			if (index > 0) {
				result += ", ";
			}
			result += FormatFloat(component(
				index, index == 3 ? 1.0f : 0.0f));
		}
		result += ")";
		return result;
	}

	GraphExpression ConvertExpression(
		GraphExpression expression, ShaderGraphValueType target) {

		if (!IsNumeric(expression.type) || !IsNumeric(target)) {
			return expression.type == target ?
				expression : GraphExpression{};
		}
		if (expression.type == target ||
			(expression.type == ShaderGraphValueType::Color &&
				target == ShaderGraphValueType::Float4) ||
			(expression.type == ShaderGraphValueType::Float4 &&
				target == ShaderGraphValueType::Color)) {

			expression.type = target;
			return expression;
		}

		const uint32_t sourceCount = ComponentCount(expression.type);
		const uint32_t targetCount = ComponentCount(target);
		const std::string source = "(" + expression.code + ")";
		if (targetCount == 1) {
			return GraphExpression{ target, source + ".x" };
		}
		if (sourceCount == 1) {
			const std::string swizzle =
				targetCount == 2 ? ".xx" :
				(targetCount == 3 ? ".xxx" : ".xxxx");
			return GraphExpression{ target, source + swizzle };
		}
		if (targetCount == 2) {
			return GraphExpression{ target, source + ".xy" };
		}
		if (targetCount == 3) {
			return sourceCount >= 3 ?
				GraphExpression{ target, source + ".xyz" } :
				GraphExpression{ target, "float3(" + expression.code + ", 0.0f)" };
		}
		if (sourceCount == 3) {
			return GraphExpression{
				target, "float4(" + expression.code + ", 1.0f)" };
		}
		if (sourceCount == 2) {
			return GraphExpression{
				target, "float4(" + expression.code + ", 0.0f, 1.0f)" };
		}
		return GraphExpression{};
	}

	bool ValidateGraphStructure(
		const ShaderGraphAsset& graph,
		ShaderGraphCompileOutput& output) {

		auto addDiagnostic = [&](Engine::UUID node, std::string message) {
			output.diagnostics.emplace_back(ShaderGraphDiagnostic{
				.node = node,
				.message = std::move(message),
				});
		};

		std::unordered_set<uint64_t> parameterIDs{};
		std::unordered_set<std::string> parameterNames{};
		for (const ShaderGraphParameter& parameter : graph.parameters) {
			if (!parameter.id ||
				!parameterIDs.insert(parameter.id.value).second) {

				addDiagnostic(parameter.id,
					"公開パラメータIDが未設定か重複しています");
			}
			if (parameter.name.empty() ||
				!parameterNames.insert(parameter.name).second) {

				addDiagnostic(parameter.id,
					"公開パラメータ名が未設定か重複しています");
			}
			if (parameter.type == ShaderGraphValueType::Invalid) {
				addDiagnostic(parameter.id,
					"公開パラメータの型が不正です");
			}
		}

		std::unordered_map<uint64_t, const ShaderGraphNode*> nodes{};
		for (const ShaderGraphNode& node : graph.nodes) {
			if (!node.id ||
				!nodes.emplace(node.id.value, &node).second) {

				addDiagnostic(node.id,
					"ノードIDが未設定か重複しています");
				continue;
			}
			if (node.kind == ShaderGraphNodeKind::Parameter &&
				(!node.parameterID ||
					!parameterIDs.contains(node.parameterID.value))) {

				addDiagnostic(node.id,
					"Parameterノードの公開パラメータが見つかりません");
			}
			if (node.kind == ShaderGraphNodeKind::CustomFunction &&
				node.customFunctionSource ==
					ShaderGraphCustomFunctionSource::File &&
				node.functionFile.empty()) {

				addDiagnostic(node.id,
					"Custom FunctionのHLSLファイルが未設定です");
			}
		}

		std::unordered_set<std::string> keywordNames{};
		for (const ShaderGraphKeyword& keyword : graph.keywords) {
			if (!keyword.id || keyword.referenceName.empty() ||
				!keywordNames.insert(keyword.referenceName).second) {

				addDiagnostic(keyword.id,
					"KeywordのIDまたは参照名が未設定か重複しています");
			}
		}

		if (!graph.outputNode ||
			!nodes.contains(graph.outputNode.value)) {

			addDiagnostic(graph.outputNode,
				"出力ノードが見つかりません");
		}

		std::unordered_set<uint64_t> linkIDs{};
		for (const ShaderGraphLink& link : graph.links) {
			if (!link.id ||
				!linkIDs.insert(link.id.value).second) {

				addDiagnostic(link.inputNode,
					"リンクIDが未設定か重複しています");
			}

			const auto outputNode =
				nodes.find(link.outputNode.value);
			const auto inputNode =
				nodes.find(link.inputNode.value);
			if (outputNode == nodes.end() ||
				inputNode == nodes.end()) {

				addDiagnostic(link.inputNode,
					"リンク先のノードが見つかりません");
				continue;
			}
			if (link.outputSlot >=
				GetShaderGraphOutputCount(
					*outputNode->second)) {

				addDiagnostic(link.outputNode,
					"リンク元の出力ピンが範囲外です");
			}
			if (link.inputSlot >=
				GetShaderGraphInputCount(
					*inputNode->second)) {

				addDiagnostic(link.inputNode,
					"リンク先の入力ピンが範囲外です");
			}
		}
		return output.diagnostics.empty();
	}

	void BuildSamplerBindings(const ShaderGraphAsset& graph,
		ShaderGraphCompileOutput& output) {

		std::vector<const ShaderGraphNode*> samplerNodes;
		for (const ShaderGraphNode& node : graph.nodes) {
			if (node.kind == ShaderGraphNodeKind::SamplerState) {
				samplerNodes.emplace_back(&node);
			}
		}
		std::sort(samplerNodes.begin(), samplerNodes.end(),
			[](const ShaderGraphNode* lhs,
				const ShaderGraphNode* rhs) {
				return lhs->id.value < rhs->id.value;
			});

		uint32_t shaderRegister = 1;
		for (const ShaderGraphNode* node : samplerNodes) {
			output.samplers.emplace_back(ShaderGraphSamplerBinding{
				.node = node->id,
				.shaderName = MakeNodeVariable("gSampler", node->id),
				.shaderRegister = shaderRegister++,
				.settings = node->sampler,
			});
		}
	}

	class CompilerContext {
	public:

		CompilerContext(
			const ShaderGraphAsset& graph,
			ShaderGraphCompileOutput& output) :
			graph_(graph), output_(output) {

			for (const ShaderGraphNode& node : graph.nodes) {
				nodes_[node.id.value] = &node;
			}
			for (const ShaderGraphSamplerBinding& sampler : output.samplers) {
				samplerNames_[sampler.node.value] = sampler.shaderName;
			}
			for (const ShaderGraphParameter& parameter : graph.parameters) {
				parameters_[parameter.id.value] = &parameter;
				parameterFields_[parameter.id.value] =
					MakeIdentifier(
						parameter.referenceName.empty() ?
						parameter.name : parameter.referenceName,
						parameter.id);
			}
			for (const ShaderGraphKeyword& keyword : graph.keywords) {
				keywords_[keyword.id.value] = &keyword;
				if (keyword.runtimeToggle) {
					keywordFields_[keyword.id.value] =
						MakeIdentifier(
							keyword.referenceName.empty() ?
							keyword.name : keyword.referenceName,
							keyword.id);
				}
			}
			for (const ShaderGraphLink& link : graph.links) {
				const InputKey key{
					.node = link.inputNode.value,
					.slot = link.inputSlot,
				};
				if (incoming_.contains(key)) {
					AddDiagnostic(link.inputNode,
						"入力ピンへ複数のリンクが接続されています");
					continue;
				}
				incoming_[key] = &link;
			}
		}

		const ShaderGraphNode* FindNode(Engine::UUID id) const {

			const auto found = nodes_.find(id.value);
			return found != nodes_.end() ? found->second : nullptr;
		}

		GraphExpression EmitInput(
			const ShaderGraphNode& node, uint32_t slot,
			ShaderGraphValueType target,
			std::string_view fallback) {

			const auto found = incoming_.find(InputKey{
				.node = node.id.value,
				.slot = slot,
				});
			if (found == incoming_.end()) {
				return GraphExpression{ target, std::string(fallback) };
			}
			const ShaderGraphLink& link = *found->second;
			GraphExpression expression =
				EmitNode(link.outputNode, link.outputSlot);
			GraphExpression converted =
				ConvertExpression(std::move(expression), target);
			if (converted.type == ShaderGraphValueType::Invalid) {
				AddDiagnostic(node.id,
					"接続された値の型を入力ピンへ変換できません");
			}
			return converted;
		}

		GraphExpression EmitNode(
			Engine::UUID nodeID, uint32_t outputSlot) {

			const NodeOutputKey cacheKey{
				.node = nodeID.value,
				.slot = outputSlot,
			};
			if (const auto found = cache_.find(cacheKey);
				found != cache_.end()) {

				return found->second;
			}
			const ShaderGraphNode* node = FindNode(nodeID);
			if (!node) {
				AddDiagnostic(nodeID, "リンク先のノードが見つかりません");
				return {};
			}
			if (!visiting_.insert(nodeID.value).second) {
				AddDiagnostic(nodeID, "グラフに循環参照があります");
				return {};
			}

			GraphExpression result = EmitNodeExpression(*node, outputSlot);
			visiting_.erase(nodeID.value);
			cache_[cacheKey] = result;
			return result;
		}

		std::string BuildParameterStructure() const {

			std::string source =
				"struct ShaderGraphParameters {\n\n";
			if (graph_.parameters.empty() &&
				keywordFields_.empty()) {
				source += "\tuint unused;\n";
			}
			for (const ShaderGraphParameter& parameter : graph_.parameters) {
				source += "\t" + HLSLType(parameter.type) + " " +
					parameterFields_.at(parameter.id.value) + ";\n";
			}
			AppendKeywordFields(source);
			source += "};\n";
			return source;
		}

		std::string BuildSamplerDeclarations() const {

			std::string source;
			for (const ShaderGraphSamplerBinding& sampler : output_.samplers) {
				source += "SamplerState " + sampler.shaderName +
					" : register(s" +
					std::to_string(sampler.shaderRegister) + ");\n";
			}
			return source;
		}

		std::string BuildMaterialConstantBuffer(
			uint32_t bindPoint = 3,
			std::string_view bufferName = "MaterialParameters") const {

			std::string source =
				"cbuffer " + std::string(bufferName) + " : register(b" +
				std::to_string(bindPoint) + ") {\n\n";
			if (graph_.parameters.empty() &&
				keywordFields_.empty()) {
				source += "\tuint unused;\n";
			}
			for (const ShaderGraphParameter& parameter : graph_.parameters) {
				source += "\t" + HLSLType(parameter.type) + " " +
					parameterFields_.at(parameter.id.value) + ";\n";
			}
			AppendKeywordFields(source);
			source += "};\n";
			return source;
		}

		std::string BuildMaterialParameterGetter() const {

			std::string source =
				"ShaderGraphParameters GetShaderGraphParameters() {\n\n"
				"\tShaderGraphParameters result;\n";
			if (graph_.parameters.empty() &&
				keywordFields_.empty()) {
				source += "\tresult.unused = unused;\n";
			}
			for (const ShaderGraphParameter& parameter : graph_.parameters) {
				const std::string& field =
					parameterFields_.at(parameter.id.value);
				source += "\tresult." + field + " = " + field + ";\n";
			}
			for (const ShaderGraphKeyword& keyword : graph_.keywords) {
				if (!keyword.runtimeToggle) {
					continue;
				}
				const std::string& field =
					keywordFields_.at(keyword.id.value);
				source += "\tresult." + field + " = " + field + ";\n";
			}
			source +=
				"\treturn result;\n"
				"}\n";
			return source;
		}

		std::string BuildCustomFunctionDeclarations() const {

			std::string source;
			std::unordered_set<std::string> files;
			for (const ShaderGraphNode& node : graph_.nodes) {
				if (node.kind != ShaderGraphNodeKind::CustomFunction) {
					continue;
				}
				if (node.customFunctionSource ==
					ShaderGraphCustomFunctionSource::File) {
					if (!node.functionFile.empty() &&
						files.insert(node.functionFile).second) {
						source += "#include \"" + node.functionFile + "\"\n";
					}
				} else if (!node.functionBody.empty()) {
					source += node.functionBody + "\n";
				}
			}
			return source;
		}

		const std::string& GetEvaluationStatements() const {

			return evaluationStatements_;
		}

		void AddDiagnostic(
			Engine::UUID node, std::string message) {

			output_.diagnostics.emplace_back(ShaderGraphDiagnostic{
				.node = node,
				.message = std::move(message),
				});
		}
	private:

		void AppendKeywordFields(std::string& source) const {

			for (const ShaderGraphKeyword& keyword : graph_.keywords) {
				if (!keyword.runtimeToggle) {
					continue;
				}
				source += "\t" + std::string(
					keyword.type == ShaderGraphKeywordType::Boolean ?
						"uint" : "int") + " " +
					keywordFields_.at(keyword.id.value) + ";\n";
			}
		}

		GraphExpression EmitNodeExpression(
			const ShaderGraphNode& node, uint32_t outputSlot) {

			switch (node.kind) {
			case ShaderGraphNodeKind::Parameter: {
				const auto found = parameters_.find(node.parameterID.value);
				if (found == parameters_.end()) {
					AddDiagnostic(node.id,
						"Parameterノードの公開パラメータが見つかりません");
					return {};
				}
				const ShaderGraphParameter& parameter = *found->second;
				return GraphExpression{
					parameter.type,
					"graphParameters." +
						parameterFields_.at(parameter.id.value),
				};
			}
			case ShaderGraphNodeKind::Constant:
				return GraphExpression{
					node.valueType,
					MakeLiteral(node.value, node.valueType),
				};
			case ShaderGraphNodeKind::Keyword: {
				const auto found = keywords_.find(node.keywordID.value);
				if (found == keywords_.end()) {
					AddDiagnostic(node.id,
						"Keywordノードの定義が見つかりません");
					return {};
				}
				const ShaderGraphKeyword& keyword = *found->second;
				if (keyword.runtimeToggle) {
					return GraphExpression{
						keyword.type == ShaderGraphKeywordType::Boolean ?
							ShaderGraphValueType::Boolean :
							ShaderGraphValueType::Integer,
						"graphParameters." +
							keywordFields_.at(keyword.id.value),
					};
				}
				return GraphExpression{
					keyword.type == ShaderGraphKeywordType::Boolean ?
						ShaderGraphValueType::Boolean :
						ShaderGraphValueType::Integer,
					std::to_string(keyword.defaultIndex) + "u",
				};
			}
			case ShaderGraphNodeKind::UV:
				return GraphExpression{
					ShaderGraphValueType::Float2, "graphInput.uv" };
			case ShaderGraphNodeKind::WorldNormal:
				return GraphExpression{
					ShaderGraphValueType::Float3,
					"graphInput.worldNormal",
				};
			case ShaderGraphNodeKind::WorldPosition:
				return GraphExpression{
					ShaderGraphValueType::Float3,
					"graphInput.worldPosition",
				};
			case ShaderGraphNodeKind::ObjectPosition:
				return GraphExpression{
					ShaderGraphValueType::Float3,
					"graphInput.objectPosition",
				};
			case ShaderGraphNodeKind::ObjectNormal:
				return GraphExpression{
					ShaderGraphValueType::Float3,
					"graphInput.objectNormal",
				};
			case ShaderGraphNodeKind::ObjectTangent:
				return GraphExpression{
					ShaderGraphValueType::Float3,
					"graphInput.objectTangent",
				};
			case ShaderGraphNodeKind::ViewDirection:
				return GraphExpression{
					ShaderGraphValueType::Float3,
					"graphInput.viewDirection",
				};
			case ShaderGraphNodeKind::ScreenPosition:
				return GraphExpression{
					ShaderGraphValueType::Float4,
					"graphInput.screenPosition",
				};
			case ShaderGraphNodeKind::VertexColor:
				return GraphExpression{
					ShaderGraphValueType::Float4,
					"graphInput.vertexColor",
				};
			case ShaderGraphNodeKind::Time: {
				static constexpr std::array timeValues{
					"shaderGraphTime",
					"sin(shaderGraphTime)",
					"cos(shaderGraphTime)",
					"shaderGraphDeltaTime",
					"shaderGraphSmoothDeltaTime",
				};
				if (timeValues.size() <= outputSlot) {
					AddDiagnostic(node.id,
						"Timeの出力ピンが不正です");
					return {};
				}
				return GraphExpression{
					ShaderGraphValueType::Float,
					timeValues[outputSlot],
				};
			}
			case ShaderGraphNodeKind::Add:
			case ShaderGraphNodeKind::Subtract:
			case ShaderGraphNodeKind::Multiply:
			case ShaderGraphNodeKind::Divide:
				return EmitBinary(node,
					node.kind == ShaderGraphNodeKind::Add ? "+" :
					(node.kind == ShaderGraphNodeKind::Subtract ? "-" :
						(node.kind == ShaderGraphNodeKind::Multiply ? "*" : "/")));
			case ShaderGraphNodeKind::Power: {
				GraphExpression a =
					EmitDynamicInput(node, 0, "0.0f");
				GraphExpression b =
					EmitDynamicInput(node, 1, "1.0f");
				if (!IsNumeric(a.type) || !IsNumeric(b.type)) {
					AddDiagnostic(node.id,
						"Powerノードには数値を接続してください");
					return {};
				}
				const ShaderGraphValueType resultType =
					ComponentCount(a.type) >= ComponentCount(b.type) ?
						a.type : b.type;
				a = ConvertExpression(std::move(a), resultType);
				b = ConvertExpression(std::move(b), resultType);
				return GraphExpression{
					resultType,
					"pow(" + a.code + ", " + b.code + ")",
				};
			}
			case ShaderGraphNodeKind::Minimum:
			case ShaderGraphNodeKind::Maximum:
			case ShaderGraphNodeKind::Dot:
			case ShaderGraphNodeKind::Cross:
			case ShaderGraphNodeKind::Distance:
			case ShaderGraphNodeKind::Reflect: {
				GraphExpression a = EmitDynamicInput(node, 0, "0.0f");
				GraphExpression b = EmitDynamicInput(node, 1, "0.0f");
				if (!IsNumeric(a.type) || !IsNumeric(b.type)) {
					AddDiagnostic(node.id,
						"ベクトル演算ノードには数値を接続してください");
					return {};
				}
				const ShaderGraphValueType resultType =
					ComponentCount(a.type) >= ComponentCount(b.type) ?
					a.type : b.type;
				a = ConvertExpression(std::move(a), resultType);
				b = ConvertExpression(std::move(b), resultType);
				if (node.kind == ShaderGraphNodeKind::Dot ||
					node.kind == ShaderGraphNodeKind::Distance) {
					return GraphExpression{
						ShaderGraphValueType::Float,
						std::string(node.kind == ShaderGraphNodeKind::Dot ? "dot(" : "distance(") +
							a.code + ", " + b.code + ")",
					};
				}
				if (node.kind == ShaderGraphNodeKind::Cross) {
					a = ConvertExpression(std::move(a), ShaderGraphValueType::Float3);
					b = ConvertExpression(std::move(b), ShaderGraphValueType::Float3);
					return { ShaderGraphValueType::Float3,
						"cross(" + a.code + ", " + b.code + ")" };
				}
				const char* functionName =
					node.kind == ShaderGraphNodeKind::Minimum ? "min" :
					(node.kind == ShaderGraphNodeKind::Maximum ? "max" : "reflect");
				return { resultType,
					std::string(functionName) + "(" + a.code + ", " + b.code + ")" };
			}
			case ShaderGraphNodeKind::Lerp: {
				GraphExpression a = EmitDynamicInput(node, 0, "0.0f");
				GraphExpression b = EmitDynamicInput(node, 1, "0.0f");
				const ShaderGraphValueType resultType =
					ComponentCount(a.type) >= ComponentCount(b.type) ?
					a.type : b.type;
				a = ConvertExpression(std::move(a), resultType);
				b = ConvertExpression(std::move(b), resultType);
				GraphExpression t =
					EmitInput(node, 2, ShaderGraphValueType::Float, "0.5f");
				return a.type != ShaderGraphValueType::Invalid &&
					b.type != ShaderGraphValueType::Invalid ?
					GraphExpression{
						resultType,
						"lerp(" + a.code + ", " + b.code +
							", " + t.code + ")",
					} :
					GraphExpression{};
			}
			case ShaderGraphNodeKind::OneMinus:
			case ShaderGraphNodeKind::Saturate:
			case ShaderGraphNodeKind::Sine:
			case ShaderGraphNodeKind::Cosine:
			case ShaderGraphNodeKind::Absolute:
			case ShaderGraphNodeKind::Floor:
			case ShaderGraphNodeKind::Fraction:
			case ShaderGraphNodeKind::SquareRoot:
			case ShaderGraphNodeKind::Negate:
			case ShaderGraphNodeKind::Normalize:
			case ShaderGraphNodeKind::Length: {
				GraphExpression input =
					EmitDynamicInput(node, 0, "0.0f");
				if (!IsNumeric(input.type)) {
					AddDiagnostic(node.id,
						"単項演算ノードには数値を接続してください");
					return {};
				}
				if (node.kind == ShaderGraphNodeKind::Length) {
					return { ShaderGraphValueType::Float,
						"length(" + input.code + ")" };
				}
				const char* functionName =
					node.kind == ShaderGraphNodeKind::Saturate ? "saturate" :
					(node.kind == ShaderGraphNodeKind::Sine ? "sin" :
					(node.kind == ShaderGraphNodeKind::Cosine ? "cos" :
					(node.kind == ShaderGraphNodeKind::Absolute ? "abs" :
					(node.kind == ShaderGraphNodeKind::Floor ? "floor" :
					(node.kind == ShaderGraphNodeKind::Fraction ? "frac" :
					(node.kind == ShaderGraphNodeKind::SquareRoot ? "sqrt" :
					(node.kind == ShaderGraphNodeKind::Normalize ? "normalize" : nullptr)))))));
				return GraphExpression{
					input.type,
					node.kind == ShaderGraphNodeKind::OneMinus ?
						"(1.0f - (" + input.code + "))" :
						(node.kind == ShaderGraphNodeKind::Negate ?
							"-(" + input.code + ")" :
							std::string(functionName) + "(" + input.code + ")"),
				};
			}
			case ShaderGraphNodeKind::Clamp:
			case ShaderGraphNodeKind::Smoothstep: {
				GraphExpression input = EmitDynamicInput(
					node, node.kind == ShaderGraphNodeKind::Clamp ? 0u : 2u,
					"0.0f");
				if (!IsNumeric(input.type)) {
					AddDiagnostic(node.id,
						"範囲演算ノードには数値を接続してください");
					return {};
				}
				const uint32_t firstSlot =
					node.kind == ShaderGraphNodeKind::Clamp ? 1u : 0u;
				GraphExpression minimum = ConvertExpression(
					EmitDynamicInput(node, firstSlot, "0.0f"), input.type);
				GraphExpression maximum = ConvertExpression(
					EmitDynamicInput(node, firstSlot + 1u, "1.0f"), input.type);
				return { input.type,
					std::string(node.kind == ShaderGraphNodeKind::Clamp ? "clamp(" : "smoothstep(") +
						minimum.code + ", " + maximum.code + ", " + input.code + ")" };
			}
			case ShaderGraphNodeKind::Step: {
				GraphExpression edge = EmitDynamicInput(node, 0, "0.5f");
				GraphExpression input = EmitDynamicInput(node, 1, "0.0f");
				const ShaderGraphValueType resultType =
					ComponentCount(edge.type) >= ComponentCount(input.type) ?
					edge.type : input.type;
				edge = ConvertExpression(std::move(edge), resultType);
				input = ConvertExpression(std::move(input), resultType);
				return { resultType,
					"step(" + edge.code + ", " + input.code + ")" };
			}
			case ShaderGraphNodeKind::Branch: {
				GraphExpression predicate = EmitDynamicInput(node, 0, "0.0f");
				GraphExpression trueValue = EmitDynamicInput(node, 1, "0.0f");
				GraphExpression falseValue = EmitDynamicInput(node, 2, "0.0f");
				const ShaderGraphValueType resultType =
					ComponentCount(trueValue.type) >= ComponentCount(falseValue.type) ?
					trueValue.type : falseValue.type;
				trueValue = ConvertExpression(std::move(trueValue), resultType);
				falseValue = ConvertExpression(std::move(falseValue), resultType);
				return { resultType,
					"((" + predicate.code + ") != 0 ? " + trueValue.code + " : " + falseValue.code + ")" };
			}
			case ShaderGraphNodeKind::Remap: {
				GraphExpression input =
					EmitDynamicInput(node, 0, "0.0f");
				if (!IsNumeric(input.type)) {
					AddDiagnostic(node.id,
						"Remapノードには数値を接続してください");
					return {};
				}
				const GraphExpression inputRange =
					EmitInput(node, 1,
						ShaderGraphValueType::Float2,
						"float2(-1.0f, 1.0f)");
				const GraphExpression outputRange =
					EmitInput(node, 2,
						ShaderGraphValueType::Float2,
						"float2(0.0f, 1.0f)");
				const std::string normalized =
					"((" + input.code + " - (" +
					inputRange.code + ").x) / ((" +
					inputRange.code + ").y - (" +
					inputRange.code + ").x))";
				return GraphExpression{
					input.type,
					"((" + outputRange.code + ").x + " +
						normalized + " * ((" +
						outputRange.code + ").y - (" +
						outputRange.code + ").x))",
				};
			}
			case ShaderGraphNodeKind::TilingAndOffset: {
				const GraphExpression uv =
					EmitInput(node, 0,
						ShaderGraphValueType::Float2,
						"graphInput.uv");
				const GraphExpression tiling =
					EmitInput(node, 1,
						ShaderGraphValueType::Float2,
						"float2(1.0f, 1.0f)");
				const GraphExpression offset =
					EmitInput(node, 2,
						ShaderGraphValueType::Float2,
						"float2(0.0f, 0.0f)");
				return GraphExpression{
					ShaderGraphValueType::Float2,
					"((" + uv.code + " * " +
						tiling.code + ") + " +
						offset.code + ")",
				};
			}
			case ShaderGraphNodeKind::PolarCoordinates: {
				const GraphExpression uv =
					EmitInput(node, 0,
						ShaderGraphValueType::Float2,
						"graphInput.uv");
				const GraphExpression center =
					EmitInput(node, 1,
						ShaderGraphValueType::Float2,
						"float2(0.5f, 0.5f)");
				const GraphExpression radialScale =
					EmitInput(node, 2,
						ShaderGraphValueType::Float,
						"1.0f");
				const GraphExpression lengthScale =
					EmitInput(node, 3,
						ShaderGraphValueType::Float,
						"1.0f");
				const std::string delta =
					"((" + uv.code + ") - (" +
					center.code + "))";
				return GraphExpression{
					ShaderGraphValueType::Float2,
					"float2(length(" + delta +
						") * 2.0f * " +
						radialScale.code +
						", atan2((" + delta +
						").x, (" + delta +
						").y) * 0.159154943f * " +
						lengthScale.code + ")",
				};
			}
			case ShaderGraphNodeKind::Rotate: {
				const GraphExpression uv = EmitInput(node, 0,
					ShaderGraphValueType::Float2, "graphInput.uv");
				const GraphExpression center = EmitInput(node, 1,
					ShaderGraphValueType::Float2, "float2(0.5f, 0.5f)");
				const GraphExpression rotation = EmitInput(node, 2,
					ShaderGraphValueType::Float, "0.0f");
				const std::string delta = "((" + uv.code + ") - (" + center.code + "))";
				return { ShaderGraphValueType::Float2,
					"(mul(" + delta + ", float2x2(cos(" + rotation.code + "), -sin(" + rotation.code + "), sin(" + rotation.code + "), cos(" + rotation.code + "))) + " + center.code + ")" };
			}
			case ShaderGraphNodeKind::Fresnel: {
				const GraphExpression normal = EmitInput(node, 0,
					ShaderGraphValueType::Float3, "graphInput.worldNormal");
				const GraphExpression view = EmitInput(node, 1,
					ShaderGraphValueType::Float3, "graphInput.viewDirection");
				const GraphExpression power = EmitInput(node, 2,
					ShaderGraphValueType::Float, "5.0f");
				return { ShaderGraphValueType::Float,
					"pow(1.0f - saturate(dot(normalize(" + normal.code + "), normalize(" + view.code + "))), " + power.code + ")" };
			}
			case ShaderGraphNodeKind::SimpleNoise: {
				const GraphExpression uv = EmitInput(node, 0,
					ShaderGraphValueType::Float2, "graphInput.uv");
				const GraphExpression scale = EmitInput(node, 1,
					ShaderGraphValueType::Float, "10.0f");
				return { ShaderGraphValueType::Float,
					"ShaderGraphSimpleNoise(" + uv.code + " * " + scale.code + ")" };
			}
			case ShaderGraphNodeKind::Voronoi: {
				const GraphExpression uv = EmitInput(node, 0,
					ShaderGraphValueType::Float2, "graphInput.uv");
				const GraphExpression angle = EmitInput(node, 1,
					ShaderGraphValueType::Float, "0.0f");
				const GraphExpression density = EmitInput(node, 2,
					ShaderGraphValueType::Float, "5.0f");
				const std::string value = "ShaderGraphVoronoi(" + uv.code + " * " + density.code + ", " + angle.code + ")";
				return outputSlot == 0 ?
					GraphExpression{ ShaderGraphValueType::Float, "(" + value + ").x" } :
					GraphExpression{ ShaderGraphValueType::Float, "(" + value + ").y" };
			}
			case ShaderGraphNodeKind::Split: {
				const GraphExpression input =
					EmitInput(node, 0,
						ShaderGraphValueType::Float4,
						"float4(0.0f, 0.0f, 0.0f, 0.0f)");
				static constexpr std::array components{
					'x', 'y', 'z', 'w',
				};
				if (components.size() <= outputSlot) {
					AddDiagnostic(node.id,
						"Splitの出力ピンが不正です");
					return {};
				}
				return GraphExpression{
					ShaderGraphValueType::Float,
					"(" + input.code + ")." +
						std::string(1, components[outputSlot]),
				};
			}
			case ShaderGraphNodeKind::Combine: {
				const GraphExpression r =
					EmitInput(node, 0,
						ShaderGraphValueType::Float, "0.0f");
				const GraphExpression g =
					EmitInput(node, 1,
						ShaderGraphValueType::Float, "0.0f");
				const GraphExpression b =
					EmitInput(node, 2,
						ShaderGraphValueType::Float, "0.0f");
				const GraphExpression a =
					EmitInput(node, 3,
						ShaderGraphValueType::Float, "1.0f");
				if (outputSlot == 0) {
					return GraphExpression{
						ShaderGraphValueType::Float4,
						"float4(" + r.code + ", " +
							g.code + ", " +
							b.code + ", " +
							a.code + ")",
					};
				}
				if (outputSlot == 1) {
					return GraphExpression{
						ShaderGraphValueType::Float3,
						"float3(" + r.code + ", " +
							g.code + ", " +
							b.code + ")",
					};
				}
				if (outputSlot == 2) {
					return GraphExpression{
						ShaderGraphValueType::Float2,
						"float2(" + r.code + ", " +
							g.code + ")",
					};
				}
				AddDiagnostic(node.id,
					"Combineの出力ピンが不正です");
				return {};
			}
			case ShaderGraphNodeKind::TextureSample: {
				auto sampleFound =
					textureSampleVariables_.find(
						node.id.value);
				if (sampleFound ==
					textureSampleVariables_.end()) {

					GraphExpression texture =
						EmitInput(
							node, 0,
							ShaderGraphValueType::Texture2D,
							"kNoTexture");
					GraphExpression uv =
						EmitInput(
							node, 1,
							ShaderGraphValueType::Float2,
							"graphInput.uv");
					GraphExpression sampler =
						EmitInput(
							node, 2,
							ShaderGraphValueType::SamplerState,
							"gSampler");
					const std::string variable =
						MakeNodeVariable(
							"sample", node.id);
					evaluationStatements_ +=
						"\tconst float4 " + variable +
						" = SampleGraphTexture(" +
						texture.code + ", " + uv.code +
						", " + sampler.code +
						", " +
						MakeLiteral(
							node.value,
							ShaderGraphValueType::Float4) +
						");\n";
					sampleFound =
						textureSampleVariables_.
						emplace(
							node.id.value,
							variable).first;
				}
				const std::string& sample =
					sampleFound->second;
				switch (outputSlot) {
				case 0: return { ShaderGraphValueType::Float4, sample };
				case 1: return { ShaderGraphValueType::Float3, "(" + sample + ").rgb" };
				case 2: return { ShaderGraphValueType::Float, "(" + sample + ").r" };
				case 3: return { ShaderGraphValueType::Float, "(" + sample + ").g" };
				case 4: return { ShaderGraphValueType::Float, "(" + sample + ").b" };
				case 5: return { ShaderGraphValueType::Float, "(" + sample + ").a" };
				default:
					AddDiagnostic(node.id, "TextureSampleの出力ピンが不正です");
					return {};
				}
			}
			case ShaderGraphNodeKind::SamplerState: {
				const auto found = samplerNames_.find(node.id.value);
				if (found == samplerNames_.end() || outputSlot != 0) {
					AddDiagnostic(node.id,
						"Sampler Stateの出力ピンが不正です");
					return {};
				}
				return {
					ShaderGraphValueType::SamplerState,
					found->second,
				};
			}
			case ShaderGraphNodeKind::NormalUnpack: {
				GraphExpression input =
					EmitInput(node, 0, ShaderGraphValueType::Float4, "float4(0.5f, 0.5f, 1.0f, 1.0f)");
				return GraphExpression{
					ShaderGraphValueType::Float3,
					"normalize(mul(((" + input.code +
						").xyz * 2.0f - 1.0f), graphInput.tangentToWorld))",
				};
			}
			case ShaderGraphNodeKind::SceneColor:
			case ShaderGraphNodeKind::SceneMaterial:
			case ShaderGraphNodeKind::SceneEmissive: {
				GraphExpression uv = EmitInput(
					node, 0, ShaderGraphValueType::Float2,
					"graphInput.uv");
				const char* textureName = nullptr;
				switch (node.kind) {
				case ShaderGraphNodeKind::SceneColor:
					textureName = graph_.domain == ShaderGraphDomain::PostProcess ?
						"gSourceColor" : ShaderGraphBindingNames::kSceneColor;
					break;
				case ShaderGraphNodeKind::SceneMaterial:
					textureName = ShaderGraphBindingNames::kSceneMaterial;
					break;
				case ShaderGraphNodeKind::SceneEmissive:
					textureName = ShaderGraphBindingNames::kSceneEmissive;
					break;
				default:
					break;
				}
				const std::string sample = std::string(textureName) +
					".SampleLevel(gSampler, " + uv.code + ", 0.0f)";
				switch (outputSlot) {
				case 0: return { ShaderGraphValueType::Float4, sample };
				case 1: return { ShaderGraphValueType::Float3, "(" + sample + ").rgb" };
				case 2: return { ShaderGraphValueType::Float, "(" + sample + ").r" };
				case 3: return { ShaderGraphValueType::Float, "(" + sample + ").g" };
				case 4: return { ShaderGraphValueType::Float, "(" + sample + ").b" };
				case 5: return { ShaderGraphValueType::Float, "(" + sample + ").a" };
				default:
					AddDiagnostic(node.id, "Scene Textureの出力ピンが不正です");
					return {};
				}
			}
			case ShaderGraphNodeKind::SceneDepth:
			case ShaderGraphNodeKind::SceneFlags: {
				const GraphExpression uv = EmitInput(
					node, 0, ShaderGraphValueType::Float2,
					"graphInput.uv");
				if (outputSlot != 0) {
					AddDiagnostic(node.id, "Scene Textureの出力ピンが不正です");
					return {};
				}
				if (node.kind == ShaderGraphNodeKind::SceneFlags) {
					const std::string pixel =
						graph_.domain == ShaderGraphDomain::PostProcess ?
						"uint2(saturate(" + uv.code + ") * "
							"max(resolution - 1.0f.xx, 0.0f.xx))" :
						"uint2(graphInput.screenPosition.xy)";
					return {
						ShaderGraphValueType::Float,
						"(float)" + std::string(ShaderGraphBindingNames::kSceneFlags) +
						".Load(int3(" + pixel + ", 0))",
					};
				}
				return {
					ShaderGraphValueType::Float,
					std::string(ShaderGraphBindingNames::kSceneDepth) +
					".SampleLevel(gSampler, " + uv.code + ", 0.0f)",
				};
			}
			case ShaderGraphNodeKind::SceneNormal:
			case ShaderGraphNodeKind::ScenePosition: {
				const GraphExpression uv = EmitInput(
					node, 0, ShaderGraphValueType::Float2,
					"graphInput.uv");
				if (outputSlot != 0) {
					AddDiagnostic(node.id, "Scene Textureの出力ピンが不正です");
					return {};
				}
				const char* textureName =
					node.kind == ShaderGraphNodeKind::SceneNormal ?
					ShaderGraphBindingNames::kSceneNormal :
					ShaderGraphBindingNames::kScenePosition;
				return {
					ShaderGraphValueType::Float3,
					std::string(textureName) +
					".SampleLevel(gSampler, " + uv.code + ", 0.0f).xyz",
				};
			}
			case ShaderGraphNodeKind::RayTrace: {
				if (graph_.domain != ShaderGraphDomain::RayTracingEffect) {
					AddDiagnostic(node.id,
						"Trace SceneはRayTracingEffectでのみ使用できます");
					return {};
				}
				auto cached = customFunctionOutputs_.find(node.id.value);
				if (cached == customFunctionOutputs_.end()) {
					const GraphExpression origin = EmitInput(node, 0,
						ShaderGraphValueType::Float3,
						"graphInput.worldPosition");
					const GraphExpression direction = EmitInput(node, 1,
						ShaderGraphValueType::Float3,
						"-graphInput.viewDirection");
					const GraphExpression minDistance = EmitInput(node, 2,
						ShaderGraphValueType::Float, "0.001f");
					const GraphExpression maxDistance = EmitInput(node, 3,
						ShaderGraphValueType::Float,
						"gMaxReflectionDistance");
					const GraphExpression mask = EmitInput(node, 4,
						ShaderGraphValueType::Integer,
						"int(kRaytracingMaskReflectionCaster)");
					const std::string variable = MakeNodeVariable(
						"trace", node.id);
					evaluationStatements_ +=
						"\tShaderGraphRayResult " + variable +
						" = ShaderGraphTraceScene(" + origin.code + ", " +
						direction.code + ", " + minDistance.code + ", " +
						maxDistance.code + ", (uint)(" + mask.code + "));\n";
					std::vector<GraphExpression> outputs{
						{ ShaderGraphValueType::Float3, variable + ".color" },
						{ ShaderGraphValueType::Float, variable + ".hit" },
						{ ShaderGraphValueType::Float, variable + ".distance" },
						{ ShaderGraphValueType::Float3, variable + ".position" },
						{ ShaderGraphValueType::Float3, variable + ".normal" },
					};
					cached = customFunctionOutputs_.emplace(
						node.id.value, std::move(outputs)).first;
				}
				if (outputSlot >= cached->second.size()) {
					AddDiagnostic(node.id,
						"Trace Sceneの出力ピンが範囲外です");
					return {};
				}
				return cached->second[outputSlot];
			}
			case ShaderGraphNodeKind::CustomFunction: {
				if (node.functionName.empty() || node.outputPorts.empty()) {
					AddDiagnostic(node.id,
						"Custom Functionの関数名または出力が未設定です");
					return {};
				}
				if (outputSlot >= node.outputPorts.size()) {
					AddDiagnostic(node.id,
						"Custom Functionの出力ピンが範囲外です");
					return {};
				}
				auto cached = customFunctionOutputs_.find(node.id.value);
				if (cached == customFunctionOutputs_.end()) {
					std::string arguments;
					for (uint32_t slot = 0; slot < node.inputPorts.size(); ++slot) {
						if (!arguments.empty()) {
							arguments += ", ";
						}
						arguments += EmitInput(node, slot,
							node.inputPorts[slot].type,
							MakeLiteral(node.inputPorts[slot].defaultValue,
								node.inputPorts[slot].type)).code;
					}

					std::vector<GraphExpression> outputs;
					outputs.reserve(node.outputPorts.size());
					for (uint32_t slot = 0; slot < node.outputPorts.size(); ++slot) {
						const ShaderGraphPort& port = node.outputPorts[slot];
						const std::string variable = MakeNodeVariable(
							"custom" + std::to_string(slot), node.id);
						evaluationStatements_ += "\t" + HLSLType(port.type) +
							" " + variable + " = " +
							MakeLiteral(port.defaultValue, port.type) + ";\n";
						if (!arguments.empty()) {
							arguments += ", ";
						}
						arguments += variable;
						outputs.emplace_back(GraphExpression{ port.type, variable });
					}
					evaluationStatements_ += "\t" + node.functionName +
						"(" + arguments + ");\n";
					cached = customFunctionOutputs_.emplace(
						node.id.value, std::move(outputs)).first;
				}
				return cached->second[outputSlot];
			}
			case ShaderGraphNodeKind::SubGraph:
				AddDiagnostic(node.id,
					"Sub Graphがコンパイル前に展開されていません");
				return {};
			case ShaderGraphNodeKind::SurfaceOutput:
			case ShaderGraphNodeKind::UnlitOutput:
			case ShaderGraphNodeKind::PostProcessOutput:
			case ShaderGraphNodeKind::RayTracingOutput:
			case ShaderGraphNodeKind::VertexOutput:
				AddDiagnostic(node.id, "Outputノードは値として接続できません");
				return {};
			}
			return {};
		}

		GraphExpression EmitDynamicInput(
			const ShaderGraphNode& node, uint32_t slot,
			std::string_view fallback) {

			const auto found = incoming_.find(InputKey{
				.node = node.id.value,
				.slot = slot,
				});
			if (found == incoming_.end()) {
				return GraphExpression{
					ShaderGraphValueType::Float,
					std::string(fallback),
				};
			}
			return EmitNode(
				found->second->outputNode,
				found->second->outputSlot);
		}

		GraphExpression EmitBinary(
			const ShaderGraphNode& node,
			std::string_view operation) {

			GraphExpression a = EmitDynamicInput(node, 0, "0.0f");
			GraphExpression b = EmitDynamicInput(node, 1, "0.0f");
			if (!IsNumeric(a.type) || !IsNumeric(b.type)) {
				AddDiagnostic(node.id,
					"演算ノードには数値を接続してください");
				return {};
			}
			const ShaderGraphValueType resultType =
				ComponentCount(a.type) >= ComponentCount(b.type) ?
				a.type : b.type;
			a = ConvertExpression(std::move(a), resultType);
			b = ConvertExpression(std::move(b), resultType);
			return GraphExpression{
				resultType,
				"(" + a.code + " " + std::string(operation) +
					" " + b.code + ")",
			};
		}

		const ShaderGraphAsset& graph_;
		ShaderGraphCompileOutput& output_;
		std::unordered_map<uint64_t, const ShaderGraphNode*> nodes_;
		std::unordered_map<uint64_t, const ShaderGraphParameter*> parameters_;
		std::unordered_map<uint64_t, const ShaderGraphKeyword*> keywords_;
		std::unordered_map<uint64_t, std::string> parameterFields_;
		std::unordered_map<uint64_t, std::string> keywordFields_;
		std::unordered_map<uint64_t, std::string> samplerNames_;
		std::unordered_map<InputKey, const ShaderGraphLink*, InputKeyHasher> incoming_;
		// ノードと出力ピンの組を保持し、共有式の再生成を避ける
		std::unordered_map<NodeOutputKey, GraphExpression, NodeOutputKeyHasher> cache_;
		std::unordered_map<uint64_t, std::string>
			textureSampleVariables_;
		std::unordered_map<uint64_t, std::vector<GraphExpression>>
			customFunctionOutputs_;
		std::unordered_set<uint64_t> visiting_;
		std::string evaluationStatements_;
	};

	std::string BuildSurfaceSource(
		const ShaderGraphAsset& graph,
		CompilerContext& context) {

		const ShaderGraphNode* output =
			context.FindNode(graph.outputNode);
		const bool is3D = IsShaderGraph3DTarget(graph.target);
		const ShaderGraphNodeKind expectedOutput =
			is3D ? ShaderGraphNodeKind::SurfaceOutput :
			ShaderGraphNodeKind::UnlitOutput;
		if (!output || output->kind != expectedOutput) {
			context.AddDiagnostic(graph.outputNode,
				is3D ?
				"PBR Surface出力ノードが見つかりません" :
				"Unlit Surface出力ノードが見つかりません");
			return {};
		}

		const GraphExpression baseColor =
			context.EmitInput(*output, 0,
				ShaderGraphValueType::Float4,
				"float4(1.0f, 1.0f, 1.0f, 1.0f)");
		const GraphExpression normal =
			is3D ?
			context.EmitInput(*output, 1,
				ShaderGraphValueType::Float3,
				"graphInput.worldNormal") :
			GraphExpression{
				ShaderGraphValueType::Float3,
				"graphInput.worldNormal",
			};
		const GraphExpression metallic =
			is3D ?
			context.EmitInput(*output, 2,
				ShaderGraphValueType::Float, "0.0f") :
			GraphExpression{ ShaderGraphValueType::Float, "0.0f" };
		const GraphExpression roughness =
			is3D ?
			context.EmitInput(*output, 3,
				ShaderGraphValueType::Float, "0.5f") :
			GraphExpression{ ShaderGraphValueType::Float, "0.5f" };
		const GraphExpression ao =
			is3D ?
			context.EmitInput(*output, 4,
				ShaderGraphValueType::Float, "1.0f") :
			GraphExpression{ ShaderGraphValueType::Float, "1.0f" };
		const GraphExpression emissive =
			is3D ?
			context.EmitInput(*output, 5,
				ShaderGraphValueType::Float3, "0.0f.xxx") :
			GraphExpression{
				ShaderGraphValueType::Float3, "0.0f.xxx",
			};
		const GraphExpression opacity =
			context.EmitInput(*output, is3D ? 6u : 1u,
				ShaderGraphValueType::Float, "1.0f");
		const GraphExpression alphaClip =
			context.EmitInput(*output, is3D ? 7u : 2u,
				ShaderGraphValueType::Float, "0.0f");

		std::string source =
			"#ifndef NEM_GENERATED_SHADER_GRAPH_SURFACE\n"
			"#define NEM_GENERATED_SHADER_GRAPH_SURFACE\n\n";
		source +=
			"Texture2D<float4> gShaderGraphSceneColor : register(t0, space4);\n"
			"Texture2D<float> gShaderGraphSceneDepth : register(t1, space4);\n"
			"Texture2D<float4> gShaderGraphSceneNormal : register(t2, space4);\n"
			"Texture2D<float4> gShaderGraphScenePosition : register(t3, space4);\n"
			"Texture2D<float4> gShaderGraphSceneMaterial : register(t4, space4);\n"
			"Texture2D<float4> gShaderGraphSceneEmissive : register(t5, space4);\n"
			"Texture2D<uint> gShaderGraphSceneFlags : register(t6, space4);\n\n"
			"cbuffer ShaderGraphTimeConstants : register(b4) {\n\n"
			"\tfloat shaderGraphTime;\n"
			"\tfloat shaderGraphDeltaTime;\n"
			"\tfloat shaderGraphSmoothDeltaTime;\n"
			"\tfloat shaderGraphUnscaledTime;\n"
			"};\n\n";
		source += context.BuildSamplerDeclarations();
		source += context.BuildParameterStructure();
		source +=
			"\nstruct ShaderGraphSurfaceInput {\n\n"
			"\tfloat2 uv;\n"
			"\tfloat3 worldNormal;\n"
			"\tfloat3 worldPosition;\n"
			"\tfloat3 objectPosition;\n"
			"\tfloat3 objectNormal;\n"
			"\tfloat3 objectTangent;\n"
			"\tfloat3 viewDirection;\n"
			"\tfloat4 screenPosition;\n"
			"\tfloat4 vertexColor;\n"
			"\tfloat3x3 tangentToWorld;\n"
			"};\n\n"
			"struct ShaderGraphSurface {\n\n"
			"\tfloat4 baseColor;\n"
			"\tfloat3 normal;\n"
			"\tfloat metallic;\n"
			"\tfloat roughness;\n"
			"\tfloat ambientOcclusion;\n"
			"\tfloat3 emissive;\n"
			"\tfloat opacity;\n"
			"\tfloat alphaClip;\n"
			"};\n\n";
		const bool particleTarget =
			graph.target == ShaderGraphTarget::Particle ||
			graph.target == ShaderGraphTarget::Trail;
		if (particleTarget) {
			uint32_t textureRegister = 1;
			for (const ShaderGraphParameter& parameter : graph.parameters) {
				if (parameter.type != ShaderGraphValueType::Texture2D) {
					continue;
				}
				source += "Texture2D<float4> " + MakeIdentifier(
					parameter.referenceName.empty() ? parameter.name : parameter.referenceName,
					parameter.id) + " : register(t" + std::to_string(textureRegister++) +
					", space2);\n";
			}
			source +=
				"\nfloat4 SampleGraphTexture(uint textureIndex, float2 uv, SamplerState sampler, float4 fallbackValue) {\n\n";
			uint32_t textureIndex = 0;
			for (const ShaderGraphParameter& parameter : graph.parameters) {
				if (parameter.type != ShaderGraphValueType::Texture2D) {
					continue;
				}
				source += "\tif (textureIndex == " + std::to_string(textureIndex++) +
					"u) return " + MakeIdentifier(
						parameter.referenceName.empty() ? parameter.name : parameter.referenceName,
					parameter.id) + ".SampleLevel(sampler, uv, 0.0f);\n";
			}
			source += "\treturn fallbackValue;\n}\n\n";
		} else {
			source +=
				"float4 SampleGraphTexture(uint textureIndex, float2 uv, SamplerState sampler, float4 fallbackValue) {\n\n"
				"\tif (textureIndex == 0xFFFFFFFFu) {\n"
				"\t\treturn fallbackValue;\n"
				"\t}\n"
				"\tTexture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(textureIndex)];\n"
				"\treturn texture.SampleLevel(sampler, uv, 0.0f);\n"
				"}\n\n";
		}
		source +=
			"float ShaderGraphHash(float2 value) {\n\n"
			"\treturn frac(sin(dot(value, float2(127.1f, 311.7f))) * 43758.5453f);\n"
			"}\n\n"
			"float ShaderGraphSimpleNoise(float2 uv) {\n\n"
			"\tfloat2 cell = floor(uv);\n"
			"\tfloat2 local = frac(uv);\n"
			"\tfloat2 blend = local * local * (3.0f - 2.0f * local);\n"
			"\tfloat a = ShaderGraphHash(cell);\n"
			"\tfloat b = ShaderGraphHash(cell + float2(1.0f, 0.0f));\n"
			"\tfloat c = ShaderGraphHash(cell + float2(0.0f, 1.0f));\n"
			"\tfloat d = ShaderGraphHash(cell + float2(1.0f, 1.0f));\n"
			"\treturn lerp(lerp(a, b, blend.x), lerp(c, d, blend.x), blend.y);\n"
			"}\n\n"
			"float2 ShaderGraphVoronoi(float2 uv, float angleOffset) {\n\n"
			"\tfloat2 cell = floor(uv);\n"
			"\tfloat2 local = frac(uv);\n"
			"\tfloat minimumDistance = 8.0f;\n"
			"\tfloat cellValue = 0.0f;\n"
			"\t[unroll] for (int y = -1; y <= 1; ++y) {\n"
			"\t\t[unroll] for (int x = -1; x <= 1; ++x) {\n"
			"\t\t\tfloat2 offset = float2(x, y);\n"
			"\t\t\tfloat random = ShaderGraphHash(cell + offset);\n"
			"\t\t\tfloat2 featurePoint = 0.5f + 0.5f * float2(sin(random * 6.283185307f + angleOffset), cos(random * 6.283185307f + angleOffset));\n"
			"\t\t\tfloat distanceValue = distance(local, offset + featurePoint);\n"
			"\t\t\tif (distanceValue < minimumDistance) { minimumDistance = distanceValue; cellValue = random; }\n"
			"\t\t}\n"
			"\t}\n"
			"\treturn float2(minimumDistance, cellValue);\n"
			"}\n\n";
		source += context.BuildCustomFunctionDeclarations();
		source +=
			"ShaderGraphSurface EvaluateShaderGraphSurface(\n"
			"\tShaderGraphSurfaceInput graphInput,\n"
			"\tShaderGraphParameters graphParameters) {\n\n"
			"\tShaderGraphSurface result;\n";
		source += context.GetEvaluationStatements();
		source += "\tresult.baseColor = " + baseColor.code + ";\n";
		source += "\tresult.normal = normalize(" + normal.code + ");\n";
		source += "\tresult.metallic = saturate(" + metallic.code + ");\n";
		source += "\tresult.roughness = max(saturate(" +
			roughness.code + "), 0.04f);\n";
		source += "\tresult.ambientOcclusion = saturate(" + ao.code + ");\n";
		source += "\tresult.emissive = " + emissive.code + ";\n";
		source += "\tresult.opacity = saturate(" + opacity.code + ");\n";
		source += "\tresult.alphaClip = saturate(" + alphaClip.code + ");\n";
		source +=
			"\treturn result;\n"
			"}\n\n"
			"#endif // NEM_GENERATED_SHADER_GRAPH_SURFACE\n";
		return source;
	}

	std::string BuildMeshPixelSource(
		std::string_view surfaceIncludeFile,
		bool transparent) {

		std::string source = "// Shader Graph generated file\n";
		if (transparent) {
			source +=
				"#include \"Builtin/Mesh/Common/meshSurfaceLighting.hlsli\"\n";
		}
		else {
			source +=
				"#include \"Builtin/Mesh/Common/defaultMesh.hlsli\"\n"
				"#include \"Builtin/Mesh/Common/meshLighting.hlsli\"\n"
				"#include \"Builtin/Mesh/Common/deferredGBuffer.hlsli\"\n";
		}
		source +=
			"#include \"" + std::string(surfaceIncludeFile) + "\"\n\n"
			"StructuredBuffer<ShaderGraphParameters> gMeshMaterialParameters : register(t0, space3);\n\n"
			"ShaderGraphParameters GetShaderGraphParameters(uint instanceID, uint localSubMeshIndex) {\n\n"
			"\tMeshInstance instance = gMeshInstances[instanceID];\n"
			"\tuint safeCount = max(instance.subMeshCount, 1u);\n"
			"\tuint clampedIndex = min(localSubMeshIndex, safeCount - 1u);\n"
			"\treturn gMeshMaterialParameters[instance.subMeshDataOffset + clampedIndex];\n"
			"}\n\n"
			"ShaderGraphSurface EvaluateRasterShaderGraph(VSOutput input) {\n\n"
			"\tSubMeshShaderData subMesh = GetInstanceSubMesh(input.instanceID, input.subMeshIndex);\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = mul(float4(input.uv, 0.0f, 1.0f), subMesh.uvMatrix).xy;\n"
			"\tgraphInput.worldNormal = normalize(input.normal);\n"
			"\tgraphInput.worldPosition = input.worldPos;\n"
			"\tgraphInput.objectPosition = input.worldPos;\n"
			"\tgraphInput.objectNormal = input.normal;\n"
			"\tgraphInput.objectTangent = input.tangent;\n"
			"\tgraphInput.viewDirection = normalize(renderCameraPos - input.worldPos);\n"
			"\tgraphInput.screenPosition = input.position;\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.tangentToWorld = BuildMeshTBN(input);\n"
			"\treturn EvaluateShaderGraphSurface(graphInput,\n"
			"\t\tGetShaderGraphParameters(input.instanceID, input.subMeshIndex));\n"
			"}\n\n";

		if (!transparent) {
			source +=
				"GBufferOutput main(VSOutput input) {\n\n"
				"\tShaderGraphSurface graph = EvaluateRasterShaderGraph(input);\n"
				"\tclip(graph.baseColor.a * graph.opacity - graph.alphaClip);\n"
				"\tMeshSurface surface;\n"
				"\tsurface.albedo = graph.baseColor.rgb;\n"
				"\tsurface.normal = graph.normal;\n"
				"\tsurface.worldPos = input.worldPos;\n"
				"\tsurface.metallic = graph.metallic;\n"
				"\tsurface.roughness = graph.roughness;\n"
				"\tsurface.occlusion = graph.ambientOcclusion;\n"
				"\tsurface.emissive = graph.emissive;\n"
				"\tsurface.motion = ComputeGBufferMotion(input.currentClipPosition, input.previousClipPosition);\n"
				"\tsurface.flags = BuildMaterialFlags(gMeshInstances[input.instanceID].flags);\n"
				"\treturn EncodeGBuffer(surface);\n"
				"}\n";
		} else {
			source +=
				"struct TransparentPSOutput {\n\n"
				"\tfloat4 color : SV_TARGET0;\n"
				"};\n\n"
				"TransparentPSOutput mainTransparent(VSOutput input) {\n\n"
				"\tShaderGraphSurface graph = EvaluateRasterShaderGraph(input);\n"
				"\tfloat alpha = graph.baseColor.a * graph.opacity;\n"
				"\tclip(alpha - graph.alphaClip);\n"
				"\tResolvedPBRMaterial material;\n"
				"\tmaterial.baseColor = graph.baseColor;\n"
				"\tmaterial.N = graph.normal;\n"
				"\tmaterial.metallic = graph.metallic;\n"
				"\tmaterial.roughness = graph.roughness;\n"
				"\tmaterial.ao = graph.ambientOcclusion;\n"
				"\tmaterial.emissive = graph.emissive;\n"
				"\tTransparentPSOutput output;\n"
				"\toutput.color = float4(EvaluateMeshSurfaceLighting(input, material), alpha);\n"
				"\treturn output;\n"
				"}\n";
		}
		return source;
	}

	std::string BuildRayTracingSource(
		std::string_view surfaceIncludeFile,
		const CompilerContext& context) {

		std::string source =
			"// Shader Graph generated Ray Tracing library\n"
			"#define NEM_REFLECTION_CUSTOM_HIT\n"
			"#include \"Builtin/Raytracing/reflection.RT.hlsl\"\n"
			"#include \"" + std::string(surfaceIncludeFile) + "\"\n\n";
		source += context.BuildMaterialConstantBuffer(
			5, "RayTracingParameters");
		source += "\n" + context.BuildMaterialParameterGetter() + "\n";
		source +=
			"ShaderGraphSurface EvaluateRayTracingShaderGraph(\n"
			"\tin BuiltInTriangleIntersectionAttributes attr) {\n\n"
			"\tRaytracingInstanceShaderData instanceData =\n"
			"\t\tgRaytracingSceneInstances[InstanceID()];\n"
			"\tRaytracingGeometryShaderData geometryData =\n"
			"\t\tgRaytracingGeometries[instanceData.geometryDataOffset + GeometryIndex()];\n"
			"\tSubMeshShaderData subMesh =\n"
			"\t\tgRaytracingSubMeshes[geometryData.subMeshDataIndex];\n"
			"\tStructuredBuffer<uint> indices =\n"
			"\t\tResourceDescriptorHeap[NonUniformResourceIndex(instanceData.indexDescriptorIndex)];\n"
			"\tStructuredBuffer<MeshVertex> vertices =\n"
			"\t\tResourceDescriptorHeap[NonUniformResourceIndex(instanceData.vertexDescriptorIndex)];\n"
			"\tuint baseIndex = geometryData.indexOffset + PrimitiveIndex() * 3u;\n"
			"\tMeshVertex v0 = vertices[instanceData.vertexOffset + indices[baseIndex + 0u]];\n"
			"\tMeshVertex v1 = vertices[instanceData.vertexOffset + indices[baseIndex + 1u]];\n"
			"\tMeshVertex v2 = vertices[instanceData.vertexOffset + indices[baseIndex + 2u]];\n"
			"\tfloat3 bary = ComputeBarycentrics(attr.barycentrics);\n"
			"\tfloat2 uv = v0.uv * bary.x + v1.uv * bary.y + v2.uv * bary.z;\n"
			"\tfloat3 objectPosition = v0.position.xyz * bary.x +\n"
			"\t\tv1.position.xyz * bary.y + v2.position.xyz * bary.z;\n"
			"\tfloat3 objectNormal = normalize(v0.normal * bary.x +\n"
			"\t\tv1.normal * bary.y + v2.normal * bary.z);\n"
			"\tfloat3 objectTangent = normalize(v0.tangent * bary.x +\n"
			"\t\tv1.tangent * bary.y + v2.tangent * bary.z);\n"
			"\tfloat tangentSign = v0.tangentSign * bary.x +\n"
			"\t\tv1.tangentSign * bary.y + v2.tangentSign * bary.z < 0.0f ? -1.0f : 1.0f;\n"
			"\tfloat3 localNormal = normalize(mul(float4(objectNormal, 0.0f),\n"
			"\t\tsubMesh.localNormalMatrix).xyz);\n"
			"\tfloat3 localTangent = normalize(mul(float4(objectTangent, 0.0f),\n"
			"\t\tsubMesh.localMatrix).xyz);\n"
			"\tfloat3x3 objectToWorld = (float3x3)ObjectToWorld3x4();\n"
			"\tfloat3x3 worldToObject = (float3x3)WorldToObject3x4();\n"
			"\tfloat3 worldNormal = normalize(mul(localNormal, worldToObject));\n"
			"\tfloat3 worldTangent = normalize(mul(\n"
			"\t\tobjectToWorld, localTangent));\n"
			"\tworldTangent = normalize(worldTangent -\n"
			"\t\tworldNormal * dot(worldNormal, worldTangent));\n"
			"\tfloat worldOrientationSign = determinant(objectToWorld) < 0.0f ? -1.0f : 1.0f;\n"
			"\tfloat bitangentSign = tangentSign *\n"
			"\t\tsubMesh.localOrientationSign * worldOrientationSign;\n"
			"\tfloat3 worldBitangent = normalize(\n"
			"\t\tcross(worldNormal, worldTangent)) * bitangentSign;\n"
			"\tif (dot(worldNormal, WorldRayDirection()) > 0.0f) {\n"
			"\t\tworldNormal = -worldNormal;\n"
			"\t\tworldBitangent = -worldBitangent;\n"
			"\t}\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = mul(float4(uv, 0.0f, 1.0f), subMesh.uvMatrix).xy;\n"
			"\tgraphInput.worldNormal = worldNormal;\n"
			"\tgraphInput.worldPosition = WorldRayOrigin() +\n"
			"\t\tWorldRayDirection() * RayTCurrent();\n"
			"\tgraphInput.objectPosition = mul(float4(objectPosition, 1.0f),\n"
			"\t\tsubMesh.localMatrix).xyz;\n"
			"\tgraphInput.objectNormal = localNormal;\n"
			"\tgraphInput.objectTangent = localTangent;\n"
			"\tgraphInput.viewDirection = normalize(-WorldRayDirection());\n"
			"\tgraphInput.screenPosition = float4(DispatchRaysIndex().xy, 0.0f, 1.0f);\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.tangentToWorld = float3x3(\n"
			"\t\tworldTangent, worldBitangent, worldNormal);\n"
			"\treturn EvaluateShaderGraphSurface(\n"
			"\t\tgraphInput, GetShaderGraphParameters());\n"
			"}\n\n"
			"[shader(\"anyhit\")]\n"
			"void ReflectionAnyHit(inout ReflectionPayload payload,\n"
			"\tin BuiltInTriangleIntersectionAttributes attr) {\n\n"
			"\tShaderGraphSurface graph = EvaluateRayTracingShaderGraph(attr);\n"
			"\tif (graph.baseColor.a * graph.opacity < graph.alphaClip) {\n"
			"\t\tIgnoreHit();\n"
			"\t}\n"
			"}\n\n"
			"[shader(\"closesthit\")]\n"
			"void ReflectionClosestHit(inout ReflectionPayload payload,\n"
			"\tin BuiltInTriangleIntersectionAttributes attr) {\n\n"
			"\tShaderGraphSurface graph = EvaluateRayTracingShaderGraph(attr);\n"
			"\tRaytracingInstanceShaderData instanceData =\n"
			"\t\tgRaytracingSceneInstances[InstanceID()];\n"
			"\tfloat3 worldPosition = WorldRayOrigin() +\n"
			"\t\tWorldRayDirection() * RayTCurrent();\n"
			"\tResolvedPBRMaterial material;\n"
			"\tmaterial.baseColor = graph.baseColor;\n"
			"\tmaterial.N = graph.normal;\n"
			"\tmaterial.metallic = graph.metallic;\n"
			"\tmaterial.roughness = graph.roughness;\n"
			"\tmaterial.ao = graph.ambientOcclusion;\n"
			"\tmaterial.emissive = graph.emissive;\n"
			"\tpayload.hit = 1u;\n"
			"\tpayload.color = EvaluateRaytracingSurfaceLighting(\n"
			"\t\tworldPosition, material, instanceData.renderFlags);\n"
			"\tpayload.worldPosition = worldPosition;\n"
			"\tpayload.hitDistance = RayTCurrent();\n"
			"\tpayload.worldNormal = graph.normal;\n"
			"\tpayload._pad0 = 0.0f;\n"
			"}\n";
		return source;
	}

	std::string BuildMeshAuxiliaryPixelSource(
		std::string_view surfaceIncludeFile,
		bool picking) {

		std::string source =
			"// Shader Graph generated Mesh auxiliary file\n"
			"#include \"Builtin/Mesh/Common/defaultMesh.hlsli\"\n"
			"SamplerState gSampler : register(s0);\n"
			"#include \"" + std::string(surfaceIncludeFile) + "\"\n\n"
			"StructuredBuffer<ShaderGraphParameters> gMeshMaterialParameters : register(t0, space3);\n\n"
			"ShaderGraphParameters GetShaderGraphParameters(uint instanceID, uint localSubMeshIndex) {\n\n"
			"\tMeshInstance instance = gMeshInstances[instanceID];\n"
			"\tuint safeCount = max(instance.subMeshCount, 1u);\n"
			"\tuint clampedIndex = min(localSubMeshIndex, safeCount - 1u);\n"
			"\treturn gMeshMaterialParameters[instance.subMeshDataOffset + clampedIndex];\n"
			"}\n\n"
			"ShaderGraphSurface EvaluateRasterShaderGraph(VSOutput input) {\n\n"
			"\tSubMeshShaderData subMesh = GetInstanceSubMesh(input.instanceID, input.subMeshIndex);\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = mul(float4(input.uv, 0.0f, 1.0f), subMesh.uvMatrix).xy;\n"
			"\tgraphInput.worldNormal = normalize(input.normal);\n"
			"\tgraphInput.worldPosition = input.worldPos;\n"
			"\tgraphInput.objectPosition = input.worldPos;\n"
			"\tgraphInput.objectNormal = input.normal;\n"
			"\tgraphInput.objectTangent = input.tangent;\n"
			"\tgraphInput.viewDirection = normalize(renderCameraPos - input.worldPos);\n"
			"\tgraphInput.screenPosition = input.position;\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.tangentToWorld = BuildMeshTBN(input);\n"
			"\treturn EvaluateShaderGraphSurface(graphInput, GetShaderGraphParameters(input.instanceID, input.subMeshIndex));\n"
			"}\n\n";
		if (picking) {
			source +=
				"uint4 main(VSOutput input) : SV_Target0 {\n\n"
				"\tShaderGraphSurface graph = EvaluateRasterShaderGraph(input);\n"
				"\tclip(graph.baseColor.a * graph.opacity - graph.alphaClip);\n"
				"\tMeshInstance instance = gMeshInstances[input.instanceID];\n"
				"\treturn uint4(instance.entityIndex, instance.entityGeneration, input.subMeshIndex, 1u);\n"
				"}\n";
		} else {
			source +=
				"void main(VSOutput input) {\n\n"
				"\tShaderGraphSurface graph = EvaluateRasterShaderGraph(input);\n"
				"\tclip(graph.baseColor.a * graph.opacity - graph.alphaClip);\n"
				"}\n";
		}
		return source;
	}

	std::string BuildMeshVertexCommonSource(
		const ShaderGraphAsset& graph,
		std::string_view surfaceIncludeFile,
		CompilerContext& context) {

		const ShaderGraphNode* output =
			context.FindNode(graph.vertexOutputNode);
		if (!output || output->kind != ShaderGraphNodeKind::VertexOutput) {
			context.AddDiagnostic(graph.vertexOutputNode,
				"Vertex出力ノードが見つかりません");
			return {};
		}
		const GraphExpression position = context.EmitInput(
			*output, 0, ShaderGraphValueType::Float3,
			"vertex.position.xyz");
		const GraphExpression normal = context.EmitInput(
			*output, 1, ShaderGraphValueType::Float3,
			"vertex.normal");
		const GraphExpression tangent = context.EmitInput(
			*output, 2, ShaderGraphValueType::Float3,
			"vertex.tangent");

		std::string source =
			"// Shader Graph generated Mesh vertex file\n"
			"#include \"Builtin/Mesh/Common/defaultMesh.hlsli\"\n"
			"SamplerState gSampler : register(s0);\n"
			"#include \"" + std::string(surfaceIncludeFile) + "\"\n\n"
			"StructuredBuffer<ShaderGraphParameters> gMeshMaterialParameters : register(t0, space3);\n\n"
			"ShaderGraphParameters GetShaderGraphVertexParameters(uint instanceID, uint localSubMeshIndex) {\n\n"
			"\tMeshInstance instance = gMeshInstances[instanceID];\n"
			"\tuint safeCount = max(instance.subMeshCount, 1u);\n"
			"\tuint clampedIndex = min(localSubMeshIndex, safeCount - 1u);\n"
			"\treturn gMeshMaterialParameters[instance.subMeshDataOffset + clampedIndex];\n"
			"}\n\n"
			"struct ShaderGraphVertexResult {\n\n"
			"\tfloat3 position;\n"
			"\tfloat3 normal;\n"
			"\tfloat3 tangent;\n"
			"};\n\n"
			"ShaderGraphVertexResult EvaluateShaderGraphVertex(MeshVertex vertex, uint instanceID, uint localSubMeshIndex, float4x4 worldMatrix, float4x4 normalMatrix) {\n\n"
			"\tfloat3 originalWorldPosition = mul(vertex.position, worldMatrix).xyz;\n"
			"\tfloat3 originalWorldNormal = TransformMeshNormalToWorld(vertex.normal, normalMatrix);\n"
			"\tfloat3 originalWorldTangent = TransformMeshTangentToWorld(vertex.tangent, worldMatrix);\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = vertex.uv;\n"
			"\tgraphInput.worldNormal = originalWorldNormal;\n"
			"\tgraphInput.worldPosition = originalWorldPosition;\n"
			"\tgraphInput.objectPosition = vertex.position.xyz;\n"
			"\tgraphInput.objectNormal = vertex.normal;\n"
			"\tgraphInput.objectTangent = vertex.tangent;\n"
			"\tgraphInput.viewDirection = normalize(renderCameraPos - originalWorldPosition);\n"
			"\tgraphInput.screenPosition = mul(float4(originalWorldPosition, 1.0f), viewProjection);\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.tangentToWorld = float3x3(originalWorldTangent, cross(originalWorldNormal, originalWorldTangent) * vertex.tangentSign, originalWorldNormal);\n"
			"\tShaderGraphParameters graphParameters = GetShaderGraphVertexParameters(instanceID, localSubMeshIndex);\n"
			"\tShaderGraphVertexResult result;\n";
		source += context.GetEvaluationStatements();
		source += "\tresult.position = " + position.code + ";\n";
		source += "\tresult.normal = normalize(" + normal.code + ");\n";
		source += "\tresult.tangent = normalize(" + tangent.code + ");\n";
		source +=
			"\treturn result;\n"
			"}\n\n";
		return source;
	}

	std::string BuildMeshVertexSource(
		const ShaderGraphAsset& graph,
		std::string_view surfaceIncludeFile,
		CompilerContext& context) {

		std::string source = BuildMeshVertexCommonSource(
			graph, surfaceIncludeFile, context);
		source +=
			"VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {\n\n"
			"\tMeshVertex vertex = LoadMeshVertex(instanceID, vertexID);\n"
			"\tuint localSubMeshIndex = gVertexSubMeshIndices[vertexID];\n"
			"\tfloat4x4 worldMatrix = GetInstanceSubMeshWorldMatrix(instanceID, localSubMeshIndex);\n"
			"\tfloat4x4 normalMatrix = GetInstanceSubMeshNormalMatrix(instanceID, localSubMeshIndex);\n"
			"\tShaderGraphVertexResult graph = EvaluateShaderGraphVertex(vertex, instanceID, localSubMeshIndex, worldMatrix, normalMatrix);\n"
			"\tfloat4 worldPosition = mul(float4(graph.position, 1.0f), worldMatrix);\n"
			"\tVSOutput output;\n"
			"\toutput.position = mul(worldPosition, viewProjection);\n"
			"\toutput.worldPos = worldPosition.xyz;\n"
			"\toutput.normal = TransformMeshNormalToWorld(graph.normal, normalMatrix);\n"
			"\toutput.tangent = TransformMeshTangentToWorld(graph.tangent, worldMatrix);\n"
			"\toutput.uv = vertex.uv;\n"
			"\toutput.instanceID = instanceID;\n"
			"\toutput.subMeshIndex = localSubMeshIndex;\n"
			"\toutput.tangentSign = vertex.tangentSign;\n"
			"\toutput.orientationSign = GetInstanceSubMeshOrientationSign(instanceID, localSubMeshIndex);\n"
			"\treturn output;\n"
			"}\n";
		return source;
	}

	std::string BuildMeshShaderSource(
		const ShaderGraphAsset& graph,
		std::string_view surfaceIncludeFile,
		CompilerContext& context) {

		std::string source = BuildMeshVertexCommonSource(
			graph, surfaceIncludeFile, context);
		source +=
			"groupshared float4x4 gGraphWorldMatrix;\n"
			"groupshared float4x4 gGraphNormalMatrix;\n"
			"groupshared float gGraphOrientationSign;\n\n"
			"[outputtopology(\"triangle\")]\n"
			"[numthreads(128, 1, 1)]\n"
			"void main(uint groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID, in payload MeshDispatchPayload payload, out vertices VSOutput outVerts[64], out indices uint3 outTris[124]) {\n\n"
			"\tuint meshletIndex = payload.meshletIndices[groupID.x];\n"
			"\tuint instanceIndex = payload.instanceIndices[groupID.x];\n"
			"\tMeshletDrawDesc meshlet = gMeshlets[meshletIndex];\n"
			"\tSetMeshOutputCounts(meshlet.vertexCount, meshlet.primitiveCount);\n"
			"\tuint localSubMeshIndex = meshlet.subMeshIndex;\n"
			"\tif (groupThreadID == 0) {\n"
			"\t\tgGraphWorldMatrix = GetInstanceSubMeshWorldMatrix(instanceIndex, localSubMeshIndex);\n"
			"\t\tgGraphNormalMatrix = GetInstanceSubMeshNormalMatrix(instanceIndex, localSubMeshIndex);\n"
			"\t\tgGraphOrientationSign = GetInstanceSubMeshOrientationSign(instanceIndex, localSubMeshIndex);\n"
			"\t}\n"
			"\tGroupMemoryBarrierWithGroupSync();\n"
			"\tif (groupThreadID < meshlet.primitiveCount) outTris[groupThreadID] = UnpackPrimitiveIndex(gMeshletPrimitiveIndices[meshlet.primitiveOffset + groupThreadID]);\n"
			"\tif (groupThreadID < meshlet.vertexCount) {\n"
			"\t\tuint vertexIndex = LoadMeshletVertexIndex(meshlet.vertexOffset + groupThreadID);\n"
			"\t\tMeshVertex vertex = LoadMeshVertex(instanceIndex, vertexIndex);\n"
			"\t\tShaderGraphVertexResult graph = EvaluateShaderGraphVertex(vertex, instanceIndex, localSubMeshIndex, gGraphWorldMatrix, gGraphNormalMatrix);\n"
			"\t\tfloat4 worldPosition = mul(float4(graph.position, 1.0f), gGraphWorldMatrix);\n"
			"\t\tVSOutput output;\n"
			"\t\toutput.position = mul(worldPosition, viewProjection);\n"
			"\t\toutput.worldPos = worldPosition.xyz;\n"
			"\t\toutput.normal = TransformMeshNormalToWorld(graph.normal, gGraphNormalMatrix);\n"
			"\t\toutput.tangent = TransformMeshTangentToWorld(graph.tangent, gGraphWorldMatrix);\n"
			"\t\toutput.uv = vertex.uv;\n"
			"\t\toutput.instanceID = instanceIndex;\n"
			"\t\toutput.subMeshIndex = localSubMeshIndex;\n"
			"\t\toutput.tangentSign = vertex.tangentSign;\n"
			"\t\toutput.orientationSign = gGraphOrientationSign;\n"
			"\t\toutVerts[groupThreadID] = output;\n"
			"\t}\n"
			"}\n";
		return source;
	}

	std::string BuildPrimitiveVertexCommonSource(
		const ShaderGraphAsset& graph,
		std::string_view surfaceIncludeFile,
		CompilerContext& context) {

		const ShaderGraphNode* output =
			context.FindNode(graph.vertexOutputNode);
		if (!output || output->kind != ShaderGraphNodeKind::VertexOutput) {
			context.AddDiagnostic(graph.vertexOutputNode,
				"Vertex出力ノードが見つかりません");
			return {};
		}
		const GraphExpression position = context.EmitInput(
			*output, 0, ShaderGraphValueType::Float3,
			"vertex.position.xyz");
		const GraphExpression normal = context.EmitInput(
			*output, 1, ShaderGraphValueType::Float3,
			"vertex.normal");
		const GraphExpression tangent = context.EmitInput(
			*output, 2, ShaderGraphValueType::Float3,
			"vertex.tangent");

		std::string source =
			"// Shader Graph generated Primitive vertex file\n"
			"#include \"Builtin/Primitive/primitive.hlsli\"\n"
			"#include \"Builtin/Mesh/Common/meshShaderSharedTypes.hlsli\"\n"
			"SamplerState gSampler : register(s0);\n"
			"#include \"" + std::string(surfaceIncludeFile) + "\"\n\n"
			"StructuredBuffer<MeshVertex> gVertices : register(t0);\n"
			"StructuredBuffer<PrimitiveInstance> gInstances : register(t1);\n\n";
		source += context.BuildMaterialConstantBuffer();
		source += "\n" + context.BuildMaterialParameterGetter();
		source +=
			"\nstruct ShaderGraphPrimitiveVertexResult {\n\n"
			"\tfloat3 position;\n"
			"\tfloat3 normal;\n"
			"\tfloat3 tangent;\n"
			"};\n\n"
			"ShaderGraphPrimitiveVertexResult EvaluatePrimitiveShaderGraphVertex(MeshVertex vertex, PrimitiveInstance instance) {\n\n"
			"\tfloat4 originalWorldPosition = mul(float4(vertex.position.xyz, 1.0f), instance.worldMatrix);\n"
			"\tfloat3 originalWorldNormal = normalize(mul(vertex.normal, (float3x3)instance.worldMatrix));\n"
			"\tfloat3 originalWorldTangent = normalize(mul(vertex.tangent, (float3x3)instance.worldMatrix));\n"
			"\tfloat4 vertexColor = ResolvePrimitiveVertexColor(vertex.position.xyz, instance);\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = mul(float4(vertex.uv, 0.0f, 1.0f), instance.uvMatrix).xy;\n"
			"\tgraphInput.worldNormal = originalWorldNormal;\n"
			"\tgraphInput.worldPosition = originalWorldPosition.xyz;\n"
			"\tgraphInput.objectPosition = vertex.position.xyz;\n"
			"\tgraphInput.objectNormal = vertex.normal;\n"
			"\tgraphInput.objectTangent = vertex.tangent;\n"
			"\tgraphInput.viewDirection = normalize(cameraPosition - originalWorldPosition.xyz);\n"
			"\tgraphInput.screenPosition = mul(originalWorldPosition, viewProjection);\n"
			"\tgraphInput.vertexColor = vertexColor;\n"
			"\tgraphInput.tangentToWorld = float3x3(originalWorldTangent, cross(originalWorldNormal, originalWorldTangent) * vertex.tangentSign, originalWorldNormal);\n"
			"\tShaderGraphParameters graphParameters = GetShaderGraphParameters();\n"
			"\tShaderGraphPrimitiveVertexResult result;\n";
		source += context.GetEvaluationStatements();
		source += "\tresult.position = " + position.code + ";\n";
		source += "\tresult.normal = normalize(" + normal.code + ");\n";
		source += "\tresult.tangent = normalize(" + tangent.code + ");\n";
		source +=
			"\treturn result;\n"
			"}\n\n";
		return source;
	}

	std::string BuildPrimitiveVertexSource(
		const ShaderGraphAsset& graph,
		std::string_view surfaceIncludeFile,
		CompilerContext& context) {

		std::string source = BuildPrimitiveVertexCommonSource(
			graph, surfaceIncludeFile, context);
		source +=
			"VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {\n\n"
			"\tMeshVertex vertex = gVertices[vertexID];\n"
			"\tPrimitiveInstance instance = gInstances[instanceID];\n"
			"\tShaderGraphPrimitiveVertexResult graph = EvaluatePrimitiveShaderGraphVertex(vertex, instance);\n"
			"\treturn BuildPrimitiveVertexOutput(\n"
			"\t\tgraph.position, graph.normal, graph.tangent,\n"
			"\t\tvertex.tangentSign, vertex.uv,\n"
			"\t\tResolvePrimitiveVertexColor(vertex.position.xyz, instance),\n"
			"\t\tinstance);\n"
			"}\n";
		return source;
	}

	std::string BuildPrimitiveMeshShaderSource(
		const ShaderGraphAsset& graph,
		std::string_view surfaceIncludeFile,
		CompilerContext& context) {

		std::string source = BuildPrimitiveVertexCommonSource(
			graph, surfaceIncludeFile, context);
		source +=
			"cbuffer PrimitiveMeshConstants : register(b1) {\n\n"
			"\tuint indexCount;\n"
			"\tuint3 _pad;\n"
			"};\n"
			"StructuredBuffer<uint> gIndices : register(t2);\n\n"
			"#define PRIMITIVE_GROUP_TRIANGLES 64\n\n"
			"[numthreads(PRIMITIVE_GROUP_TRIANGLES, 1, 1)]\n"
			"[outputtopology(\"triangle\")]\n"
			"void main(uint groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID, out vertices VSOutput verts[PRIMITIVE_GROUP_TRIANGLES * 3], out indices uint3 tris[PRIMITIVE_GROUP_TRIANGLES]) {\n\n"
			"\tconst uint totalTriangles = indexCount / 3u;\n"
			"\tconst uint triangleBase = groupID.x * PRIMITIVE_GROUP_TRIANGLES;\n"
			"\tconst uint triangleCount = triangleBase < totalTriangles ? min((uint)PRIMITIVE_GROUP_TRIANGLES, totalTriangles - triangleBase) : 0u;\n"
			"\tSetMeshOutputCounts(triangleCount * 3u, triangleCount);\n"
			"\tif (groupThreadID >= triangleCount) return;\n"
			"\tPrimitiveInstance instance = gInstances[groupID.y];\n"
			"\tconst uint indexBase = (triangleBase + groupThreadID) * 3u;\n"
			"\tfor (uint index = 0; index < 3u; ++index) {\n\n"
			"\t\tMeshVertex vertex = gVertices[gIndices[indexBase + index]];\n"
			"\t\tShaderGraphPrimitiveVertexResult graph = EvaluatePrimitiveShaderGraphVertex(vertex, instance);\n"
			"\t\tverts[groupThreadID * 3u + index] = BuildPrimitiveVertexOutput(\n"
			"\t\t\tgraph.position, graph.normal, graph.tangent,\n"
			"\t\t\tvertex.tangentSign, vertex.uv,\n"
			"\t\t\tResolvePrimitiveVertexColor(vertex.position.xyz, instance),\n"
			"\t\t\tinstance);\n"
			"\t}\n"
			"\ttris[groupThreadID] = uint3(groupThreadID * 3u, groupThreadID * 3u + 1u, groupThreadID * 3u + 2u);\n"
			"}\n";
		return source;
	}

	std::string BuildPrimitive2DVertexSource(
		const ShaderGraphAsset& graph,
		std::string_view surfaceIncludeFile,
		CompilerContext& context) {

		const ShaderGraphNode* output =
			context.FindNode(graph.vertexOutputNode);
		if (!output || output->kind != ShaderGraphNodeKind::VertexOutput) {
			context.AddDiagnostic(graph.vertexOutputNode,
				"Vertex出力ノードが見つかりません");
			return {};
		}
		const GraphExpression position = context.EmitInput(
			*output, 0, ShaderGraphValueType::Float3,
			"vertex.position.xyz");
		const GraphExpression normal = context.EmitInput(
			*output, 1, ShaderGraphValueType::Float3,
			"vertex.normal");
		const GraphExpression tangent = context.EmitInput(
			*output, 2, ShaderGraphValueType::Float3,
			"vertex.tangent");

		std::string source =
			"// Shader Graph generated Primitive2D vertex file\n"
			"#include \"Builtin/Primitive/primitive2D.hlsli\"\n"
			"#include \"Builtin/Mesh/Common/meshShaderSharedTypes.hlsli\"\n"
			"SamplerState gSampler : register(s0);\n"
			"#include \"" + std::string(surfaceIncludeFile) + "\"\n\n"
			"StructuredBuffer<MeshVertex> gVertices : register(t0);\n"
			"StructuredBuffer<PrimitiveInstance> gInstances : register(t1);\n\n";
		source += context.BuildMaterialConstantBuffer();
		source += "\n" + context.BuildMaterialParameterGetter();
		source +=
			"\nstruct ShaderGraphPrimitive2DVertexResult {\n\n"
			"\tfloat3 position;\n"
			"\tfloat3 normal;\n"
			"\tfloat3 tangent;\n"
			"};\n\n"
			"ShaderGraphPrimitive2DVertexResult EvaluatePrimitive2DShaderGraphVertex(MeshVertex vertex, PrimitiveInstance instance) {\n\n"
			"\tfloat4 originalWorldPosition = mul(float4(vertex.position.xyz, 1.0f), instance.worldMatrix);\n"
			"\tfloat3 originalWorldNormal = normalize(mul(vertex.normal, (float3x3)instance.worldMatrix));\n"
			"\tfloat3 originalWorldTangent = normalize(mul(vertex.tangent, (float3x3)instance.worldMatrix));\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tfloat2 localTexcoord = ResolvePrimitive2DTexcoord(vertex.uv, instance);\n"
			"\tgraphInput.uv = mul(float4(localTexcoord, 0.0f, 1.0f), instance.uvMatrix).xy;\n"
			"\tgraphInput.worldNormal = originalWorldNormal;\n"
			"\tgraphInput.worldPosition = originalWorldPosition.xyz;\n"
			"\tgraphInput.objectPosition = vertex.position.xyz;\n"
			"\tgraphInput.objectNormal = vertex.normal;\n"
			"\tgraphInput.objectTangent = vertex.tangent;\n"
			"\tgraphInput.viewDirection = normalize(cameraPosition - originalWorldPosition.xyz);\n"
			"\tgraphInput.screenPosition = mul(originalWorldPosition, viewProjection);\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.tangentToWorld = float3x3(originalWorldTangent, cross(originalWorldNormal, originalWorldTangent) * vertex.tangentSign, originalWorldNormal);\n"
			"\tShaderGraphParameters graphParameters = GetShaderGraphParameters();\n"
			"\tShaderGraphPrimitive2DVertexResult result;\n";
		source += context.GetEvaluationStatements();
		source += "\tresult.position = " + position.code + ";\n";
		source += "\tresult.normal = normalize(" + normal.code + ");\n";
		source += "\tresult.tangent = normalize(" + tangent.code + ");\n";
		source +=
			"\treturn result;\n"
			"}\n\n"
			"VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {\n\n"
			"\tMeshVertex vertex = gVertices[vertexID];\n"
			"\tPrimitiveInstance instance = gInstances[instanceID];\n"
			"\tShaderGraphPrimitive2DVertexResult graph = EvaluatePrimitive2DShaderGraphVertex(vertex, instance);\n"
			"\treturn BuildPrimitive2DVertexOutput(graph.position, vertex.uv, instance);\n"
			"}\n";
		return source;
	}

	std::string BuildPrimitivePixelSource(
		std::string_view surfaceIncludeFile,
		const CompilerContext& context,
		bool transparent) {

		std::string source =
			"// Shader Graph generated file\n"
			"#include \"Builtin/Primitive/primitive.hlsli\"\n"
			"#include \"Builtin/Mesh/Common/pbrShading.hlsli\"\n"
			"#include \"Builtin/Mesh/Common/deferredGBuffer.hlsli\"\n"
			"#include \"" + std::string(surfaceIncludeFile) + "\"\n\n";
		source += context.BuildMaterialConstantBuffer();
		source += "\n" + context.BuildMaterialParameterGetter();
		source +=
			"\nShaderGraphSurface EvaluatePrimitiveShaderGraph(VSOutput input) {\n\n"
			"\tfloat3 N = normalize(input.normal);\n"
			"\tfloat3 T = normalize(input.tangent - N * dot(N, input.tangent));\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = input.texcoord;\n"
			"\tgraphInput.worldNormal = N;\n"
			"\tgraphInput.worldPosition = input.worldPos;\n"
			"\tgraphInput.objectPosition = input.worldPos;\n"
			"\tgraphInput.objectNormal = input.normal;\n"
			"\tgraphInput.objectTangent = T;\n"
			"\tgraphInput.viewDirection = normalize(cameraPosition - input.worldPos);\n"
			"\tgraphInput.screenPosition = input.position;\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.tangentToWorld = float3x3(T, cross(N, T) * input.tangentSign, N);\n"
			"\tShaderGraphSurface graph = EvaluateShaderGraphSurface(graphInput, GetShaderGraphParameters());\n"
			"\tgraph.baseColor *= input.vertexColor;\n"
			"\treturn graph;\n"
			"}\n\n";
		if (!transparent) {
			source +=
				"GBufferOutput main(VSOutput input) {\n\n"
				"\tShaderGraphSurface graph = EvaluatePrimitiveShaderGraph(input);\n"
				"\tclip(graph.baseColor.a * graph.opacity - graph.alphaClip);\n"
				"\tMeshSurface surface;\n"
				"\tsurface.albedo = graph.baseColor.rgb;\n"
				"\tsurface.normal = graph.normal;\n"
				"\tsurface.worldPos = input.worldPos;\n"
				"\tsurface.metallic = graph.metallic;\n"
				"\tsurface.roughness = graph.roughness;\n"
				"\tsurface.occlusion = graph.ambientOcclusion;\n"
				"\tsurface.emissive = graph.emissive;\n"
				"\tsurface.motion = ComputeGBufferMotion(input.currentClipPosition, input.previousClipPosition);\n"
				"\tsurface.flags = BuildMaterialFlags(input.flags);\n"
				"\treturn EncodeGBuffer(surface);\n"
				"}\n";
		} else {
			source +=
				"struct TransparentPSOutput { float4 color : SV_TARGET0; };\n\n"
				"TransparentPSOutput mainTransparent(VSOutput input) {\n\n"
				"\tShaderGraphSurface graph = EvaluatePrimitiveShaderGraph(input);\n"
				"\tfloat alpha = graph.baseColor.a * graph.opacity;\n"
				"\tclip(alpha - graph.alphaClip);\n"
				"\tfloat3 V = normalize(cameraPosition - input.worldPos);\n"
				"\tfloat3 F0 = lerp(0.04f.xxx, graph.baseColor.rgb, graph.metallic);\n"
				"\tfloat3 lighting = 0.0f.xxx;\n"
				"\t[loop] for (uint i = 0; i < directionalCount; ++i) lighting += EvaluatePBRDirectionalLight(gDirectionalLights[i], graph.normal, V, graph.baseColor.rgb, graph.metallic, graph.roughness, F0);\n"
				"\t[loop] for (uint i = 0; i < pointCount; ++i) lighting += EvaluatePBRPointLight(gPointLights[i], input.worldPos, graph.normal, V, graph.baseColor.rgb, graph.metallic, graph.roughness, F0);\n"
				"\t[loop] for (uint i = 0; i < spotCount; ++i) lighting += EvaluatePBRSpotLight(gSpotLights[i], input.worldPos, graph.normal, V, graph.baseColor.rgb, graph.metallic, graph.roughness, F0);\n"
				"\t[loop] for (uint i = 0; i < rectCount; ++i) lighting += EvaluatePBRRectLight(gRectLights[i], input.worldPos, graph.normal, V, graph.baseColor.rgb, graph.metallic, graph.roughness, F0);\n"
				"\tfloat3 ambient = ((input.flags & MESH_INSTANCE_FLAG_RECEIVE_IBL) != 0u) ? 0.03f * graph.baseColor.rgb * graph.ambientOcclusion : 0.0f.xxx;\n"
				"\tTransparentPSOutput output;\n"
				"\toutput.color = float4(lighting + ambient + graph.emissive, alpha);\n"
				"\treturn output;\n"
				"}\n";
		}
		return source;
	}

	std::string BuildUnlitPixelSource(
		ShaderGraphTarget target,
		std::string_view surfaceIncludeFile,
		const CompilerContext& context) {

		const bool sprite = target == ShaderGraphTarget::Sprite;
		const bool text = target == ShaderGraphTarget::Text;
		std::string source = "// Shader Graph generated file\n";
		source += sprite ?
			"#include \"Builtin/Sprite/defaultSprite.hlsli\"\n" :
			(text ?
				"#include \"Builtin/Text/defaultText.hlsli\"\n" :
				"#include \"Builtin/Primitive/primitive2D.hlsli\"\n");
		source += "SamplerState gSampler : register(s0);\n";
		if (text) {
			source +=
				"Texture2D<float4> gAtlas : register(t1);\n"
				"struct PSInstance { float2 atlasSize; float pxRange; float padding0; float4x4 uvMatrix; };\n"
				"StructuredBuffer<PSInstance> gPSInstances : register(t2);\n";
		} else if (sprite) {
			source +=
				"struct PSInstance { float4x4 uvMatrix; };\n"
				"StructuredBuffer<PSInstance> gPSInstances : register(t2);\n";
		}
		source += "#include \"" + std::string(surfaceIncludeFile) + "\"\n\n";
		source += context.BuildMaterialConstantBuffer();
		source += "\n" + context.BuildMaterialParameterGetter();
		if (text) {
			source +=
				"\nfloat Median(float r, float g, float b) { return max(min(r, g), min(max(r, g), b)); }\n"
				"float ComputeScreenPxRange(float2 uv, float pxRange, float2 atlasSize) {\n"
				"\tfloat2 unitRange = float2(pxRange / atlasSize.x, pxRange / atlasSize.y);\n"
				"\treturn max(0.5f * dot(unitRange, rcp(fwidth(uv))), 1.0f);\n"
				"}\n";
		}
		source +=
			"\nstruct PSOutput { float4 color : SV_TARGET0; };\n\n"
			"PSOutput main(VSOutput input) {\n\n"
			"\tShaderGraphSurfaceInput graphInput;\n";
		if (sprite) {
			source +=
				"\tPSInstance instance = gPSInstances[input.instanceID];\n"
				"\tgraphInput.uv = mul(float4(input.texcoord, 0.0f, 1.0f), instance.uvMatrix).xy;\n";
		} else if (text) {
			source +=
				"\tPSInstance instance = gPSInstances[input.instanceID];\n"
				"\tgraphInput.uv = mul(float4(input.materialTexcoord, 0.0f, 1.0f), instance.uvMatrix).xy;\n";
		} else {
			source += "\tgraphInput.uv = input.texcoord;\n";
		}
		source +=
			"\tgraphInput.worldNormal = float3(0.0f, 0.0f, -1.0f);\n"
			"\tgraphInput.worldPosition = float3(0.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.objectPosition = float3(0.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.objectNormal = float3(0.0f, 0.0f, -1.0f);\n"
			"\tgraphInput.objectTangent = float3(1.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.viewDirection = float3(0.0f, 0.0f, -1.0f);\n"
			"\tgraphInput.screenPosition = input.position;\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.tangentToWorld = float3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);\n"
			"\tShaderGraphSurface graph = EvaluateShaderGraphSurface(graphInput, GetShaderGraphParameters());\n"
			"\tfloat alpha = graph.baseColor.a * graph.opacity;\n";
		if (text) {
			source +=
				"\tfloat3 msdf = gAtlas.Sample(gSampler, input.texcoord).rgb;\n"
				"\tfloat signedDistance = Median(msdf.r, msdf.g, msdf.b) - 0.5f;\n"
				"\tfloat coverage = saturate(ComputeScreenPxRange(input.texcoord, instance.pxRange, instance.atlasSize) * signedDistance + 0.5f);\n"
				"\talpha *= coverage;\n";
		}
		source +=
			"\tclip(alpha - max(graph.alphaClip, 1.0f / 255.0f));\n"
			"\tPSOutput output;\n"
			"\toutput.color = float4(graph.baseColor.rgb, alpha);\n"
			"\treturn output;\n"
			"}\n";
		return source;
	}

	std::string BuildParticlePixelSource(
		const ShaderGraphAsset& graph,
		std::string_view surfaceIncludeFile) {

		std::string source =
			"// Shader Graph generated Particle file\n"
			"#include \"Builtin/Particle/Common/particle.hlsli\"\n"
			"Texture2D<float4> baseColorTexture : register(t0, space2);\n"
			"SamplerState gSampler : register(s0);\n"
			"#include \"" + std::string(surfaceIncludeFile) + "\"\n\n"
			"StructuredBuffer<ShaderGraphParameters> gParticleCustomParameters : register(t1, space1);\n\n"
			"ShaderGraphParameters GetParticleShaderGraphParameters(VSOutput input) {\n\n"
			"\tShaderGraphParameters result = (ShaderGraphParameters) 0;\n"
			"\tif (input.particleIndex != 0xffffffffu) result = gParticleCustomParameters[input.particleIndex];\n";
		uint32_t textureIndex = 0;
		for (const ShaderGraphParameter& parameter : graph.parameters) {
			if (parameter.type != ShaderGraphValueType::Texture2D) {
				continue;
			}
			source += "\tresult." + MakeIdentifier(
				parameter.referenceName.empty() ? parameter.name : parameter.referenceName,
				parameter.id) + " = " + std::to_string(textureIndex++) + "u;\n";
		}
		source +=
			"\treturn result;\n"
			"}\n\n"
			"struct PSOutput { float4 color : SV_TARGET0; };\n\n"
			"PSOutput main(VSOutput input) {\n\n"
			"\tParticleMaterialData material = GetParticleMaterial(input);\n"
			"\tfloat2 uv = TransformParticleUV(input.texcoord, material);\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = uv;\n"
			"\tgraphInput.worldNormal = float3(0.0f, 0.0f, -1.0f);\n"
			"\tgraphInput.worldPosition = float3(0.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.objectPosition = float3(0.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.objectNormal = float3(0.0f, 0.0f, -1.0f);\n"
			"\tgraphInput.objectTangent = float3(1.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.viewDirection = float3(0.0f, 0.0f, -1.0f);\n"
			"\tgraphInput.screenPosition = input.position;\n"
			"\tgraphInput.vertexColor = input.vertexColor;\n"
			"\tgraphInput.tangentToWorld = float3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);\n"
			"\tShaderGraphSurface graph = EvaluateShaderGraphSurface(graphInput, GetParticleShaderGraphParameters(input));\n"
			"\tfloat4 color = baseColorTexture.Sample(gSampler, uv) * input.vertexColor * material.materialColor * graph.baseColor;\n"
			"\tcolor.a *= graph.opacity;\n"
			"\tclip(color.a - max(material.materialParams.x, graph.alphaClip));\n"
			"\tcolor.rgb += graph.emissive + material.emissive.rgb * material.emissive.w;\n"
			"\tPSOutput output;\n"
			"\toutput.color = color;\n"
			"\treturn output;\n"
			"}\n";
		return source;
	}

	std::string BuildRayTracingEffectSource(
		const ShaderGraphAsset& graph,
		CompilerContext& context) {

		const ShaderGraphNode* output = context.FindNode(graph.outputNode);
		if (!output || output->kind !=
			ShaderGraphNodeKind::RayTracingOutput) {

			context.AddDiagnostic(graph.outputNode,
				"Ray Tracing Effect出力ノードが見つかりません");
			return {};
		}
		const GraphExpression color = context.EmitInput(
			*output, 0, ShaderGraphValueType::Float4,
			"gSourceColor.SampleLevel(gSampler, graphInput.uv, 0.0f)");

		std::string source =
			"// Shader Graph generated RayTracing Feature\n"
			"#include \"Builtin/Raytracing/reflection.RT.hlsl\"\n\n"
			"#define gShaderGraphSceneColor gSourceColor\n"
			"#define gShaderGraphSceneDepth gSourceDepth\n"
			"#define gShaderGraphSceneNormal gSourceNormal\n"
			"#define gShaderGraphScenePosition gSourcePosition\n"
			"#define gShaderGraphSceneMaterial gSourceMaterial\n"
			"#define gShaderGraphSceneFlags gSourceFlags\n"
			"Texture2D<float4> gShaderGraphSceneEmissive : register(t17);\n\n"
			"cbuffer ShaderGraphTimeConstants : register(b4) {\n\n"
			"\tfloat shaderGraphTime;\n"
			"\tfloat shaderGraphDeltaTime;\n"
			"\tfloat shaderGraphSmoothDeltaTime;\n"
			"\tfloat shaderGraphUnscaledTime;\n"
			"};\n\n";
		source += context.BuildMaterialConstantBuffer(
			5, "RayTracingParameters");
		source += "\n" + context.BuildParameterStructure();
		source += "\n" + context.BuildMaterialParameterGetter();
		source += "\n" + context.BuildSamplerDeclarations();
		source +=
			"\nstruct ShaderGraphSurfaceInput {\n\n"
			"\tfloat2 uv;\n"
			"\tfloat3 worldNormal;\n"
			"\tfloat3 worldPosition;\n"
			"\tfloat3 objectPosition;\n"
			"\tfloat3 objectNormal;\n"
			"\tfloat3 objectTangent;\n"
			"\tfloat3 viewDirection;\n"
			"\tfloat4 screenPosition;\n"
			"\tfloat4 vertexColor;\n"
			"\tfloat3x3 tangentToWorld;\n"
			"};\n\n"
			"struct ShaderGraphRayResult {\n\n"
			"\tfloat3 color;\n"
			"\tfloat hit;\n"
			"\tfloat distance;\n"
			"\tfloat3 position;\n"
			"\tfloat3 normal;\n"
			"};\n\n"
			"float4 SampleGraphTexture(uint textureIndex, float2 uv, "
			"SamplerState sampler, float4 fallbackValue) {\n\n"
			"\tif (textureIndex == 0xFFFFFFFFu) return fallbackValue;\n"
			"\tTexture2D<float4> texture = ResourceDescriptorHeap["
			"NonUniformResourceIndex(textureIndex)];\n"
			"\treturn texture.SampleLevel(sampler, uv, 0.0f);\n"
			"}\n\n"
			"float ShaderGraphHash(float2 value) {\n\n"
			"\treturn frac(sin(dot(value, float2(127.1f, 311.7f))) * "
			"43758.5453f);\n"
			"}\n\n"
			"float ShaderGraphSimpleNoise(float2 uv) {\n\n"
			"\tfloat2 cell = floor(uv);\n"
			"\tfloat2 local = frac(uv);\n"
			"\tfloat2 blend = local * local * (3.0f - 2.0f * local);\n"
			"\tfloat a = ShaderGraphHash(cell);\n"
			"\tfloat b = ShaderGraphHash(cell + float2(1.0f, 0.0f));\n"
			"\tfloat c = ShaderGraphHash(cell + float2(0.0f, 1.0f));\n"
			"\tfloat d = ShaderGraphHash(cell + float2(1.0f, 1.0f));\n"
			"\treturn lerp(lerp(a, b, blend.x), "
			"lerp(c, d, blend.x), blend.y);\n"
			"}\n\n"
			"float2 ShaderGraphVoronoi(float2 uv, float angleOffset) {\n\n"
			"\tfloat2 cell = floor(uv);\n"
			"\tfloat2 local = frac(uv);\n"
			"\tfloat minimumDistance = 8.0f;\n"
			"\tfloat cellValue = 0.0f;\n"
			"\t[unroll] for (int y = -1; y <= 1; ++y) {\n"
			"\t\t[unroll] for (int x = -1; x <= 1; ++x) {\n"
			"\t\t\tfloat2 offset = float2(x, y);\n"
			"\t\t\tfloat random = ShaderGraphHash(cell + offset);\n"
			"\t\t\tfloat2 featurePoint = 0.5f + 0.5f * float2("
			"sin(random * 6.283185307f + angleOffset), "
			"cos(random * 6.283185307f + angleOffset));\n"
			"\t\t\tfloat distanceValue = distance("
			"local, offset + featurePoint);\n"
			"\t\t\tif (distanceValue < minimumDistance) { "
			"minimumDistance = distanceValue; cellValue = random; }\n"
			"\t\t}\n"
			"\t}\n"
			"\treturn float2(minimumDistance, cellValue);\n"
			"}\n\n"
			"ShaderGraphRayResult ShaderGraphTraceScene(float3 origin, "
			"float3 direction, float minDistance, float maxDistance, "
			"uint mask) {\n\n"
			"\tRayDesc ray;\n"
			"\tray.Origin = origin;\n"
			"\tray.Direction = SafeNormalize(direction, float3(0.0f, 0.0f, 1.0f));\n"
			"\tray.TMin = max(minDistance, 0.0001f);\n"
			"\tray.TMax = max(maxDistance, ray.TMin);\n"
			"\tReflectionPayload payload = (ReflectionPayload) 0;\n"
			"\tTraceRay(gSceneTLAS, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, "
			"mask, 0, 0, 0, ray, payload);\n"
			"\tShaderGraphRayResult result;\n"
			"\tresult.color = payload.hit != 0u ? payload.color : "
			"EvaluateReflectionEnvironment(ray.Direction);\n"
			"\tresult.hit = payload.hit != 0u ? 1.0f : 0.0f;\n"
			"\tresult.distance = payload.hitDistance;\n"
			"\tresult.position = payload.worldPosition;\n"
			"\tresult.normal = payload.worldNormal;\n"
			"\treturn result;\n"
			"}\n\n";
		source += context.BuildCustomFunctionDeclarations();
		source +=
			"\n[shader(\"raygeneration\")]\n"
			"void RenderFeatureRayGeneration() {\n\n"
			"\tuint2 pixel = DispatchRaysIndex().xy;\n"
			"\tuint2 dim = DispatchRaysDimensions().xy;\n"
			"\tif (any(pixel >= dim)) return;\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = (float2(pixel) + 0.5f) / float2(dim);\n"
			"\tgraphInput.worldPosition = gSourcePosition.Load(int3(pixel, 0)).xyz;\n"
			"\tgraphInput.worldNormal = DecodeWorldNormal("
			"gSourceNormal.Load(int3(pixel, 0)).xyz);\n"
			"\tgraphInput.objectPosition = graphInput.worldPosition;\n"
			"\tgraphInput.objectNormal = graphInput.worldNormal;\n"
			"\tgraphInput.objectTangent = float3(1.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.viewDirection = SafeNormalize(gCameraPosition - "
			"graphInput.worldPosition, float3(0.0f, 0.0f, 1.0f));\n"
			"\tgraphInput.screenPosition = float4(pixel, 0.0f, 1.0f);\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.tangentToWorld = float3x3(1.0f, 0.0f, 0.0f, "
			"0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);\n"
			"\tShaderGraphParameters graphParameters = "
			"GetShaderGraphParameters();\n";
		source += context.GetEvaluationStatements();
		source +=
			"\tgDestColor[pixel] = " + color.code + ";\n"
			"}\n";
		return source;
	}

	std::string BuildPostProcessSource(
		const ShaderGraphAsset& graph,
		CompilerContext& context) {

		const ShaderGraphNode* output =
			context.FindNode(graph.outputNode);
		if (!output || output->kind !=
			ShaderGraphNodeKind::PostProcessOutput) {
			context.AddDiagnostic(graph.outputNode,
				"Post Process出力ノードが見つかりません");
			return {};
		}
		const GraphExpression color = context.EmitInput(
			*output, 0, ShaderGraphValueType::Float4,
			"gSourceColor.SampleLevel(gSampler, graphInput.uv, 0.0f)");

		std::string source =
			"// Shader Graph generated PostProcess\n"
			"cbuffer PostProcessFrameConstants : register(b0) {\n\n"
			"\tfloat2 resolution;\n"
			"\tfloat2 invResolution;\n"
			"\tfloat time;\n"
			"\tfloat deltaTime;\n"
			"\tuint frameIndex;\n"
			"\tfloat _pad0;\n"
			"\tfloat cameraNear;\n"
			"\tfloat cameraFar;\n"
			"\tfloat _pad1;\n"
			"\tfloat _pad2;\n"
			"\tfloat3 cameraWorldPos;\n"
			"\tfloat _pad3;\n"
			"\tfloat4x4 cameraView;\n"
			"\tfloat4x4 cameraViewInverse;\n"
			"\tfloat4x4 cameraProjection;\n"
			"\tfloat4x4 cameraProjectionInverse;\n"
			"};\n\n";
		source += context.BuildMaterialConstantBuffer(
			1, "PostProcessParameters");
		source += "\n" + context.BuildParameterStructure();
		source += "\n" + context.BuildMaterialParameterGetter();
		source += "\n" + context.BuildSamplerDeclarations();
		source +=
			"\nTexture2D<float4> gSourceColor : register(t0);\n"
			"Texture2D<float4> gShaderGraphSceneColor : register(t1);\n"
			"Texture2D<float> gShaderGraphSceneDepth : register(t2);\n"
			"Texture2D<float4> gShaderGraphSceneNormal : register(t3);\n"
			"Texture2D<float4> gShaderGraphScenePosition : register(t4);\n"
			"Texture2D<float4> gShaderGraphSceneMaterial : register(t5);\n"
			"Texture2D<float4> gShaderGraphSceneEmissive : register(t6);\n"
			"Texture2D<uint> gShaderGraphSceneFlags : register(t7);\n"
			"RWTexture2D<float4> gDestColor : register(u0);\n"
			"SamplerState gSampler : register(s0);\n\n"
			"struct ShaderGraphSurfaceInput {\n\n"
			"\tfloat2 uv;\n"
			"\tfloat3 worldNormal;\n"
			"\tfloat3 worldPosition;\n"
			"\tfloat3 objectPosition;\n"
			"\tfloat3 objectNormal;\n"
			"\tfloat3 objectTangent;\n"
			"\tfloat3 viewDirection;\n"
			"\tfloat4 screenPosition;\n"
			"\tfloat4 vertexColor;\n"
			"\tfloat3x3 tangentToWorld;\n"
			"};\n\n"
			"float4 SampleGraphTexture(uint textureIndex, float2 uv, SamplerState sampler, float4 fallbackValue) {\n\n"
			"\tif (textureIndex == 0xFFFFFFFFu) return fallbackValue;\n"
			"\tTexture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(textureIndex)];\n"
			"\treturn texture.SampleLevel(sampler, uv, 0.0f);\n"
			"}\n\n"
			"float ShaderGraphHash(float2 value) {\n\n"
			"\treturn frac(sin(dot(value, float2(127.1f, 311.7f))) * 43758.5453f);\n"
			"}\n\n"
			"float ShaderGraphSimpleNoise(float2 uv) {\n\n"
			"\tfloat2 cell = floor(uv);\n"
			"\tfloat2 local = frac(uv);\n"
			"\tfloat2 blend = local * local * (3.0f - 2.0f * local);\n"
			"\tfloat a = ShaderGraphHash(cell);\n"
			"\tfloat b = ShaderGraphHash(cell + float2(1.0f, 0.0f));\n"
			"\tfloat c = ShaderGraphHash(cell + float2(0.0f, 1.0f));\n"
			"\tfloat d = ShaderGraphHash(cell + float2(1.0f, 1.0f));\n"
			"\treturn lerp(lerp(a, b, blend.x), lerp(c, d, blend.x), blend.y);\n"
			"}\n\n"
			"float2 ShaderGraphVoronoi(float2 uv, float angleOffset) {\n\n"
			"\tfloat2 cell = floor(uv);\n"
			"\tfloat2 local = frac(uv);\n"
			"\tfloat minimumDistance = 8.0f;\n"
			"\tfloat cellValue = 0.0f;\n"
			"\t[unroll] for (int y = -1; y <= 1; ++y) {\n"
			"\t\t[unroll] for (int x = -1; x <= 1; ++x) {\n"
			"\t\t\tfloat2 offset = float2(x, y);\n"
			"\t\t\tfloat random = ShaderGraphHash(cell + offset);\n"
			"\t\t\tfloat2 featurePoint = 0.5f + 0.5f * float2(sin(random * 6.283185307f + angleOffset), cos(random * 6.283185307f + angleOffset));\n"
			"\t\t\tfloat distanceValue = distance(local, offset + featurePoint);\n"
			"\t\t\tif (distanceValue < minimumDistance) { minimumDistance = distanceValue; cellValue = random; }\n"
			"\t\t}\n"
			"\t}\n"
			"\treturn float2(minimumDistance, cellValue);\n"
			"}\n\n";
		source += context.BuildCustomFunctionDeclarations();
		source +=
			"\n[numthreads(8, 8, 1)]\n"
			"void main(uint3 dispatchThreadID : SV_DispatchThreadID) {\n\n"
			"\tif (any(dispatchThreadID.xy >= uint2(resolution))) return;\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = (float2(dispatchThreadID.xy) + 0.5f) * invResolution;\n"
			"\tgraphInput.worldNormal = float3(0.0f, 0.0f, -1.0f);\n"
			"\tgraphInput.worldPosition = 0.0f.xxx;\n"
			"\tgraphInput.objectPosition = 0.0f.xxx;\n"
			"\tgraphInput.objectNormal = float3(0.0f, 0.0f, -1.0f);\n"
			"\tgraphInput.objectTangent = float3(1.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.viewDirection = float3(0.0f, 0.0f, -1.0f);\n"
			"\tgraphInput.screenPosition = float4(dispatchThreadID.xy, 0.0f, 1.0f);\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.tangentToWorld = float3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);\n"
			"\tShaderGraphParameters graphParameters = GetShaderGraphParameters();\n"
			"\tfloat shaderGraphTime = time;\n"
			"\tfloat shaderGraphDeltaTime = deltaTime;\n"
			"\tfloat shaderGraphSmoothDeltaTime = deltaTime;\n"
			"\tfloat shaderGraphUnscaledTime = time;\n";
		source += context.GetEvaluationStatements();
		source +=
			"\tgDestColor[dispatchThreadID.xy] = " + color.code + ";\n"
			"}\n";
		return source;
	}

	std::string BuildPixelSource(
		const ShaderGraphAsset& graph,
		std::string_view surfaceIncludeFile,
		const CompilerContext& context,
		bool transparent) {

		switch (graph.target) {
		case ShaderGraphTarget::Mesh:
			return BuildMeshPixelSource(
				surfaceIncludeFile, transparent);
		case ShaderGraphTarget::Primitive3D:
			return BuildPrimitivePixelSource(
				surfaceIncludeFile, context, transparent);
		case ShaderGraphTarget::Sprite:
		case ShaderGraphTarget::Text:
		case ShaderGraphTarget::Primitive2D:
			return BuildUnlitPixelSource(
				graph.target, surfaceIncludeFile, context);
		case ShaderGraphTarget::Particle:
		case ShaderGraphTarget::Trail:
			return BuildParticlePixelSource(
				graph, surfaceIncludeFile);
		}
		return {};
	}
}

Engine::ShaderGraphCompileOutput Engine::ShaderGraphCompiler::Compile(
	const ShaderGraphAsset& graph,
	std::string_view surfaceIncludeFile,
	const ShaderGraphAssetResolver& resolver) {

	ShaderGraphCompileOutput output{};
	ShaderGraphAsset expandedGraph = graph;
	std::unordered_set<AssetID> resolving;
	if (!ExpandSubGraphs(
		expandedGraph, resolver,
		output.diagnostics, resolving)) {
		return output;
	}
	if (expandedGraph.vertexOutputNode &&
		!SupportsShaderGraphVertexOutput(expandedGraph.target)) {

		output.diagnostics.emplace_back(ShaderGraphDiagnostic{
			.severity = ShaderGraphDiagnosticSeverity::Error,
			.stage = ShaderGraphStage::Vertex,
			.node = expandedGraph.vertexOutputNode,
			.port = UINT32_MAX,
			.message = "Vertex出力はMeshまたはPrimitiveでのみ使用できます",
		});
		return output;
	}
	if (expandedGraph.domain == ShaderGraphDomain::Surface &&
		expandedGraph.surfaceMode != ShaderGraphSurfaceMode::Transparent) {

		for (const ShaderGraphNode& node : expandedGraph.nodes) {
			if (node.kind < ShaderGraphNodeKind::SceneColor ||
				node.kind > ShaderGraphNodeKind::SceneFlags) {
				continue;
			}
			output.diagnostics.emplace_back(ShaderGraphDiagnostic{
				.severity = ShaderGraphDiagnosticSeverity::Error,
				.stage = ShaderGraphStage::Fragment,
				.node = node.id,
				.port = UINT32_MAX,
				.message = "Scene TextureはTransparent Surfaceでのみ使用できます",
			});
		}
		if (!output.Succeeded()) {
			return output;
		}
	}
	output.ir = ShaderGraphIRBuilder::Build(expandedGraph);
	output.diagnostics = output.ir.diagnostics;
	if (!output.ir.Succeeded()) {
		return output;
	}
	BuildSamplerBindings(expandedGraph, output);
	CompilerContext context(expandedGraph, output);
	if (expandedGraph.domain == ShaderGraphDomain::PostProcess) {
		output.computeHLSL =
			BuildPostProcessSource(expandedGraph, context);
	} else if (expandedGraph.domain ==
		ShaderGraphDomain::RayTracingEffect) {

		output.rayTracingHLSL =
			BuildRayTracingEffectSource(expandedGraph, context);
	} else {
		output.surfaceHLSL = BuildSurfaceSource(expandedGraph, context);
		if (!output.Succeeded()) {
			return output;
		}
		output.opaquePixelHLSL =
			BuildPixelSource(
				expandedGraph, surfaceIncludeFile, context, false);
		output.transparentPixelHLSL =
			BuildPixelSource(
				expandedGraph, surfaceIncludeFile, context, true);
		if (IsShaderGraph3DTarget(expandedGraph.target)) {
			output.rayTracingHLSL = BuildRayTracingSource(
				surfaceIncludeFile, context);
		}
		if (expandedGraph.target == ShaderGraphTarget::Mesh) {
			output.depthPixelHLSL = BuildMeshAuxiliaryPixelSource(
				surfaceIncludeFile, false);
			output.pickingPixelHLSL = BuildMeshAuxiliaryPixelSource(
				surfaceIncludeFile, true);
			if (expandedGraph.vertexOutputNode) {
				CompilerContext vertexContext(expandedGraph, output);
				output.vertexHLSL = BuildMeshVertexSource(
					expandedGraph, surfaceIncludeFile, vertexContext);
				CompilerContext meshContext(expandedGraph, output);
				output.meshHLSL = BuildMeshShaderSource(
					expandedGraph, surfaceIncludeFile, meshContext);
			}
		} else if (expandedGraph.target ==
			ShaderGraphTarget::Primitive3D &&
			expandedGraph.vertexOutputNode) {

			CompilerContext vertexContext(expandedGraph, output);
			output.vertexHLSL = BuildPrimitiveVertexSource(
				expandedGraph, surfaceIncludeFile, vertexContext);
			CompilerContext meshContext(expandedGraph, output);
			output.meshHLSL = BuildPrimitiveMeshShaderSource(
				expandedGraph, surfaceIncludeFile, meshContext);
		} else if (expandedGraph.target ==
			ShaderGraphTarget::Primitive2D &&
			expandedGraph.vertexOutputNode) {

			CompilerContext vertexContext(expandedGraph, output);
			output.vertexHLSL = BuildPrimitive2DVertexSource(
				expandedGraph, surfaceIncludeFile, vertexContext);
		}
	}
	if (!output.Succeeded()) {
		return output;
	}
	for (const ShaderGraphParameter& parameter : expandedGraph.parameters) {

		output.parameters.emplace_back(
			ShaderParameterMetadata{
				.shaderName =
					MakeIdentifier(
						parameter.referenceName.empty() ?
							parameter.name : parameter.referenceName,
						parameter.id),
				.displayName = parameter.name,
				.id =
					MaterialParameterID::FromUUID(
						parameter.id),
				.semantic = parameter.semantic,
				.isColor =
					parameter.type ==
					ShaderGraphValueType::Color,
				.isTexture =
					parameter.type ==
					ShaderGraphValueType::Texture2D,
			});
	}
	for (const ShaderGraphKeyword& keyword : expandedGraph.keywords) {
		if (!keyword.runtimeToggle) {
			continue;
		}
		output.parameters.emplace_back(
			ShaderParameterMetadata{
				.shaderName = MakeIdentifier(
					keyword.referenceName.empty() ?
						keyword.name : keyword.referenceName,
					keyword.id),
				.displayName = keyword.name,
				.id = MaterialParameterID::FromUUID(keyword.id),
				.semantic = MaterialParameterSemantic::None,
				.isColor = false,
				.isTexture = false,
			});
	}
	return output;
}

bool Engine::ShaderGraphCompileOutput::Succeeded() const {

	return std::none_of(
		diagnostics.begin(), diagnostics.end(),
		[](const ShaderGraphDiagnostic& diagnostic) {
			return diagnostic.severity ==
				ShaderGraphDiagnosticSeverity::Error;
		});
}
