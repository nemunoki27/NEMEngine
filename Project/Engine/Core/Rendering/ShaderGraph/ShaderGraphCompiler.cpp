#include "ShaderGraphCompiler.h"

//============================================================================
//	include
//============================================================================

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
					outputNode->second->kind)) {

				addDiagnostic(link.outputNode,
					"リンク元の出力ピンが範囲外です");
			}
			if (link.inputSlot >=
				GetShaderGraphInputCount(
					inputNode->second->kind)) {

				addDiagnostic(link.inputNode,
					"リンク先の入力ピンが範囲外です");
			}
		}
		return output.diagnostics.empty();
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
			for (const ShaderGraphParameter& parameter : graph.parameters) {
				parameters_[parameter.id.value] = &parameter;
				parameterFields_[parameter.id.value] =
					MakeIdentifier(parameter.name, parameter.id);
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
				"struct MeshMaterialParameters {\n\n";
			if (graph_.parameters.empty()) {
				source += "\tuint unused;\n";
			}
			for (const ShaderGraphParameter& parameter : graph_.parameters) {
				source += "\t" + HLSLType(parameter.type) + " " +
					parameterFields_.at(parameter.id.value) + ";\n";
			}
			source += "};\n";
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
			case ShaderGraphNodeKind::Add:
			case ShaderGraphNodeKind::Multiply:
				return EmitBinary(node,
					node.kind == ShaderGraphNodeKind::Add ? "+" : "*");
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
			case ShaderGraphNodeKind::Saturate: {
				GraphExpression input =
					EmitDynamicInput(node, 0, "0.0f");
				return GraphExpression{
					input.type,
					node.kind == ShaderGraphNodeKind::OneMinus ?
						"(1.0f - (" + input.code + "))" :
						"saturate(" + input.code + ")",
				};
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
					const std::string variable =
						MakeNodeVariable(
							"sample", node.id);
					evaluationStatements_ +=
						"\tconst float4 " + variable +
						" = SampleGraphTexture(" +
						texture.code + ", " + uv.code +
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
				AddDiagnostic(node.id,
					"SceneColorはPostProcessグラフでのみ使用できます");
				return {};
			case ShaderGraphNodeKind::SurfaceOutput:
			case ShaderGraphNodeKind::PostProcessOutput:
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
		std::unordered_map<uint64_t, std::string> parameterFields_;
		std::unordered_map<InputKey, const ShaderGraphLink*, InputKeyHasher> incoming_;
		// ノードと出力ピンの組を保持し、共有式の再生成を避ける
		std::unordered_map<NodeOutputKey, GraphExpression, NodeOutputKeyHasher> cache_;
		std::unordered_map<uint64_t, std::string>
			textureSampleVariables_;
		std::unordered_set<uint64_t> visiting_;
		std::string evaluationStatements_;
	};

	std::string BuildSurfaceSource(
		const ShaderGraphAsset& graph,
		CompilerContext& context) {

		const ShaderGraphNode* output =
			context.FindNode(graph.outputNode);
		if (!output || output->kind != ShaderGraphNodeKind::SurfaceOutput) {
			context.AddDiagnostic(graph.outputNode,
				"PBR Surface出力ノードが見つかりません");
			return {};
		}

		const GraphExpression baseColor =
			context.EmitInput(*output, 0,
				ShaderGraphValueType::Float4,
				"float4(1.0f, 1.0f, 1.0f, 1.0f)");
		const GraphExpression normal =
			context.EmitInput(*output, 1,
				ShaderGraphValueType::Float3,
				"graphInput.worldNormal");
		const GraphExpression metallic =
			context.EmitInput(*output, 2,
				ShaderGraphValueType::Float, "0.0f");
		const GraphExpression roughness =
			context.EmitInput(*output, 3,
				ShaderGraphValueType::Float, "0.5f");
		const GraphExpression ao =
			context.EmitInput(*output, 4,
				ShaderGraphValueType::Float, "1.0f");
		const GraphExpression emissive =
			context.EmitInput(*output, 5,
				ShaderGraphValueType::Float3, "0.0f.xxx");
		const GraphExpression opacity =
			context.EmitInput(*output, 6,
				ShaderGraphValueType::Float, "1.0f");
		const GraphExpression alphaClip =
			context.EmitInput(*output, 7,
				ShaderGraphValueType::Float, "0.0f");

		std::string source =
			"#ifndef NEM_GENERATED_SHADER_GRAPH_SURFACE\n"
			"#define NEM_GENERATED_SHADER_GRAPH_SURFACE\n\n";
		source += context.BuildParameterStructure();
		source +=
			"\nstruct ShaderGraphSurfaceInput {\n\n"
			"\tfloat2 uv;\n"
			"\tfloat3 worldNormal;\n"
			"\tfloat3 worldPosition;\n"
			"\tfloat3x3 tangentToWorld;\n"
			"};\n\n"
			"struct ShaderGraphSurface {\n\n"
			"\tResolvedPBRMaterial material;\n"
			"\tfloat opacity;\n"
			"\tfloat alphaClip;\n"
			"};\n\n"
			"float4 SampleGraphTexture(uint textureIndex, float2 uv, float4 fallbackValue) {\n\n"
			"\tif (textureIndex == kNoTexture) {\n"
			"\t\treturn fallbackValue;\n"
			"\t}\n"
			"\tTexture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(textureIndex)];\n"
			"\treturn texture.Sample(gSampler, uv);\n"
			"}\n\n"
			"ShaderGraphSurface EvaluateShaderGraphSurface(\n"
			"\tShaderGraphSurfaceInput graphInput,\n"
			"\tMeshMaterialParameters graphParameters) {\n\n"
			"\tShaderGraphSurface result;\n";
		source += context.GetEvaluationStatements();
		source += "\tresult.material.baseColor = " + baseColor.code + ";\n";
		source += "\tresult.material.N = normalize(" + normal.code + ");\n";
		source += "\tresult.material.metallic = saturate(" + metallic.code + ");\n";
		source += "\tresult.material.roughness = max(saturate(" +
			roughness.code + "), 0.04f);\n";
		source += "\tresult.material.ao = saturate(" + ao.code + ");\n";
		source += "\tresult.material.emissive = " + emissive.code + ";\n";
		source += "\tresult.opacity = saturate(" + opacity.code + ");\n";
		source += "\tresult.alphaClip = saturate(" + alphaClip.code + ");\n";
		source +=
			"\treturn result;\n"
			"}\n\n"
			"#endif // NEM_GENERATED_SHADER_GRAPH_SURFACE\n";
		return source;
	}

	std::string BuildPixelSource(
		std::string_view surfaceIncludeFile,
		bool transparent) {

		std::string source =
			"// Shader Graph generated file\n"
			"#include \"Builtin/Mesh/Common/defaultMesh.hlsli\"\n"
			"#include \"Builtin/Mesh/Common/meshLighting.hlsli\"\n"
			"#include \"Builtin/Mesh/Common/pbrShading.hlsli\"\n"
			"#include \"Builtin/Mesh/Common/deferredGBuffer.hlsli\"\n"
			"#include \"Builtin/Mesh/Common/meshSurfaceLighting.hlsli\"\n"
			"#include \"" + std::string(surfaceIncludeFile) + "\"\n\n"
			"StructuredBuffer<MeshMaterialParameters> gMeshMaterialParameters : register(t0, space3);\n\n"
			"MeshMaterialParameters GetShaderGraphParameters(uint instanceID, uint localSubMeshIndex) {\n\n"
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
			"\tgraphInput.tangentToWorld = BuildMeshTBN(input);\n"
			"\treturn EvaluateShaderGraphSurface(graphInput,\n"
			"\t\tGetShaderGraphParameters(input.instanceID, input.subMeshIndex));\n"
			"}\n\n";

		if (!transparent) {
			source +=
				"GBufferOutput main(VSOutput input) {\n\n"
				"\tShaderGraphSurface graph = EvaluateRasterShaderGraph(input);\n"
				"\tclip(graph.material.baseColor.a * graph.opacity - graph.alphaClip);\n"
				"\tMeshSurface surface;\n"
				"\tsurface.albedo = graph.material.baseColor.rgb;\n"
				"\tsurface.normal = graph.material.N;\n"
				"\tsurface.worldPos = input.worldPos;\n"
				"\tsurface.metallic = graph.material.metallic;\n"
				"\tsurface.roughness = graph.material.roughness;\n"
				"\tsurface.occlusion = graph.material.ao;\n"
				"\tsurface.emissive = graph.material.emissive;\n"
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
				"\tfloat alpha = graph.material.baseColor.a * graph.opacity;\n"
				"\tclip(alpha - graph.alphaClip);\n"
				"\tTransparentPSOutput output;\n"
				"\toutput.color = float4(EvaluateMeshSurfaceLighting(input, graph.material), alpha);\n"
				"\treturn output;\n"
				"}\n";
		}
		return source;
	}
}

Engine::ShaderGraphCompileOutput Engine::ShaderGraphCompiler::Compile(
	const ShaderGraphAsset& graph,
	std::string_view surfaceIncludeFile) {

	ShaderGraphCompileOutput output{};
	if (graph.domain != ShaderGraphDomain::Surface) {
		output.diagnostics.emplace_back(ShaderGraphDiagnostic{
			.message = "PostProcessグラフはPostProcess Graphコンパイラで処理してください",
			});
		return output;
	}
	if (!ValidateGraphStructure(graph, output)) {
		return output;
	}

	CompilerContext context(graph, output);
	output.surfaceHLSL = BuildSurfaceSource(graph, context);
	if (!output.diagnostics.empty()) {
		return output;
	}
	output.opaquePixelHLSL =
		BuildPixelSource(surfaceIncludeFile, false);
	output.transparentPixelHLSL =
		BuildPixelSource(surfaceIncludeFile, true);
	for (const ShaderGraphParameter& parameter : graph.parameters) {

		output.parameters.emplace_back(
			ShaderParameterMetadata{
				.shaderName =
					MakeIdentifier(
						parameter.name,
						parameter.id),
				.displayName = parameter.name,
				.id =
					MaterialParameterID::FromUUID(
						parameter.id),
				.semantic = parameter.semantic,
				.isColor =
					parameter.type ==
					ShaderGraphValueType::Color,
			});
	}
	return output;
}
