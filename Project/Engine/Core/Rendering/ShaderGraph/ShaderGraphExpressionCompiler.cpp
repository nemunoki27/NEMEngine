#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>

// c++
#include <utility>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

// ノードとピンを合わせてハッシュ化する
size_t ShaderGraphExpressionCompiler::EndpointKeyHasher::operator()(const EndpointKey& key) const noexcept {

	const size_t nodeHash = std::hash<uint64_t>{}(key.node);
	const size_t slotHash = std::hash<uint32_t>{}(key.slot);
	return Algorithm::MixHash(nodeHash, slotHash);
}

// Graphの索引と接続を構築する
ShaderGraphExpressionCompiler::ShaderGraphExpressionCompiler(
	const ShaderGraphAsset& graph, ShaderGraphCompileOutput& output) :
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
			MakeIdentifier(parameter.referenceName.empty() ? parameter.name : parameter.referenceName, parameter.id);
	}
	for (const ShaderGraphKeyword& keyword : graph.keywords) {
		keywords_[keyword.id.value] = &keyword;
		if (keyword.runtimeToggle) {
			keywordFields_[keyword.id.value] =
				MakeIdentifier(keyword.referenceName.empty() ? keyword.name : keyword.referenceName, keyword.id);
		}
	}
	// 入力ピンごとの接続を索引へまとめる
	for (const ShaderGraphLink& link : graph.links) {
		const EndpointKey key{
			.node = link.inputNode.value,
			.slot = link.inputSlot,
		};
		if (incoming_.contains(key)) {
			AddDiagnostic(link.inputNode, "入力ピンへ複数のリンクが接続されています");
			continue;
		}
		incoming_[key] = &link;
	}
}

// 識別子からNodeを取得する
const ShaderGraphNode* ShaderGraphExpressionCompiler::FindNode(Engine::UUID id) const {

	const auto found = nodes_.find(id.value);
	return found != nodes_.end() ? found->second : nullptr;
}

// 接続値を入力の型へ変換する
ShaderGraphExpression ShaderGraphExpressionCompiler::EmitInput(
	const ShaderGraphNode& node, uint32_t slot, ShaderGraphValueType target, std::string_view fallback) {

	const auto found = incoming_.find(EndpointKey{
		.node = node.id.value,
		.slot = slot,
	});
	if (found == incoming_.end()) {
		return ShaderGraphExpression{target, std::string(fallback)};
	}
	const ShaderGraphLink& link = *found->second;
	ShaderGraphExpression expression = EmitNode(link.outputNode, link.outputSlot);
	ShaderGraphExpression converted = ConvertExpression(std::move(expression), target);
	if (converted.type == ShaderGraphValueType::Invalid) {
		AddDiagnostic(node.id, "接続された値の型を入力ピンへ変換できません");
	}
	return converted;
}

// 循環を確認しNodeの値を生成する
ShaderGraphExpression ShaderGraphExpressionCompiler::EmitNode(Engine::UUID nodeID, uint32_t outputSlot) {

	const EndpointKey cacheKey{
		.node = nodeID.value,
		.slot = outputSlot,
	};
	// 共有された出力式は一度だけ生成する
	if (const auto found = cache_.find(cacheKey); found != cache_.end()) {
		return found->second;
	}
	const ShaderGraphNode* node = FindNode(nodeID);
	if (!node) {
		AddDiagnostic(nodeID, "リンク先のノードが見つかりません");
		return {};
	}
	// 評価中のNodeへ戻った接続を循環として扱う
	if (!visiting_.insert(nodeID.value).second) {
		AddDiagnostic(nodeID, "グラフに循環参照があります");
		return {};
	}

	ShaderGraphExpression result = EmitNodeExpression(*node, outputSlot);
	visiting_.erase(nodeID.value);
	cache_[cacheKey] = result;
	return result;
}

// 公開値の構造体を生成する
std::string ShaderGraphExpressionCompiler::BuildParameterStructure() const {

	std::string source = "struct ShaderGraphParameters {\n\n";
	AppendParameterFields(source);
	source += "};\n";
	return source;
}

// Samplerの宣言を生成する
std::string ShaderGraphExpressionCompiler::BuildSamplerDeclarations() const {

	std::string source;
	for (const ShaderGraphSamplerBinding& sampler : output_.samplers) {
		source += "SamplerState " + sampler.shaderName + " : register(s" + std::to_string(sampler.shaderRegister) + ");\n";
	}
	return source;
}

// Material定数の宣言を生成する
std::string ShaderGraphExpressionCompiler::BuildMaterialConstantBuffer(uint32_t bindPoint, std::string_view bufferName) const {

	std::string source = "cbuffer " + std::string(bufferName) + " : register(b" + std::to_string(bindPoint) + ") {\n\n";
	AppendParameterFields(source);
	source += "};\n";
	return source;
}

// 公開値とKeywordの宣言を追加する
void ShaderGraphExpressionCompiler::AppendParameterFields(std::string& source) const {

	// 空の定数領域にも型を成立させる要素を残す
	if (graph_.parameters.empty() && keywordFields_.empty()) {
		source += "\tuint unused;\n";
	}
	for (const ShaderGraphParameter& parameter : graph_.parameters) {
		source += "\t" + HLSLType(parameter.type) + " " + parameterFields_.at(parameter.id.value) + ";\n";
	}
	AppendKeywordFields(source);
}

// Material値の取得関数を生成する
std::string ShaderGraphExpressionCompiler::BuildMaterialParameterGetter(bool bindless) const {

	// 取得元に合わせてMaterial参照を生成
	const std::string prefix = bindless ? "parameters." : "";
	std::string source = bindless ? "ShaderGraphParameters GetShaderGraphParameters(uint descriptor) {\n\n" :
		"ShaderGraphParameters GetShaderGraphParameters() {\n\n";
	if (bindless) {
		source += "\tConstantBuffer<ShaderGraphParameters> parameters = ResourceDescriptorHeap[NonUniformResourceIndex(descriptor)];\n";
	}
	source += "\tShaderGraphParameters result;\n";
	if (graph_.parameters.empty() && keywordFields_.empty()) {
		source += "\tresult.unused = " + prefix + "unused;\n";
	}
	for (const ShaderGraphParameter& parameter : graph_.parameters) {
		const std::string& field = parameterFields_.at(parameter.id.value);
		source += "\tresult." + field + " = " + prefix + field + ";\n";
	}
	for (const ShaderGraphKeyword& keyword : graph_.keywords) {
		if (!keyword.runtimeToggle) {
			continue;
		}
		const std::string& field = keywordFields_.at(keyword.id.value);
		source += "\tresult." + field + " = " + prefix + field + ";\n";
	}
	source += "\treturn result;\n"
			  "}\n";
	return source;
}

// 外部関数の宣言を生成する
std::string ShaderGraphExpressionCompiler::BuildCustomFunctionDeclarations() const {

	std::string source;
	std::unordered_set<std::string> files;
	for (const ShaderGraphNode& node : graph_.nodes) {
		if (node.kind != ShaderGraphNodeKind::CustomFunction) {
			continue;
		}
		if (node.customFunctionSource == ShaderGraphCustomFunctionSource::File) {
			if (!node.functionFile.empty() && files.insert(node.functionFile).second) {
				source += "#include \"" + node.functionFile + "\"\n";
			}
		} else if (!node.functionBody.empty()) {
			source += node.functionBody + "\n";
		}
	}
	return source;
}

// 値の評価文を取得する
const std::string& ShaderGraphExpressionCompiler::GetEvaluationStatements() const {

	return evaluationStatements_;
}

// 生成時の診断を追加する
void ShaderGraphExpressionCompiler::AddDiagnostic(Engine::UUID node, std::string message) {

	output_.diagnostics.emplace_back(ShaderGraphDiagnostic{
		.node = node,
		.message = std::move(message),
	});
}

// 実行時Keywordの宣言を追加する
void ShaderGraphExpressionCompiler::AppendKeywordFields(std::string& source) const {

	for (const ShaderGraphKeyword& keyword : graph_.keywords) {
		if (!keyword.runtimeToggle) {
			continue;
		}
		source += "\t" + std::string(keyword.type == ShaderGraphKeywordType::Boolean ? "uint" : "int") + " " +
				  keywordFields_.at(keyword.id.value) + ";\n";
	}
}

// 接続値を元の型で取得する
ShaderGraphExpression ShaderGraphExpressionCompiler::EmitDynamicInput(
	const ShaderGraphNode& node, uint32_t slot, std::string_view fallback) {

	const auto found = incoming_.find(EndpointKey{
		.node = node.id.value,
		.slot = slot,
	});
	if (found == incoming_.end()) {
		return ShaderGraphExpression{
			ShaderGraphValueType::Float,
			std::string(fallback),
		};
	}
	return EmitNode(found->second->outputNode, found->second->outputSlot);
}

// 二項演算の式を作る
ShaderGraphExpression ShaderGraphExpressionCompiler::EmitBinary(const ShaderGraphNode& node, std::string_view operation) {

	ShaderGraphExpression a = EmitDynamicInput(node, 0, "0.0f");
	ShaderGraphExpression b = EmitDynamicInput(node, 1, "0.0f");
	if (!IsNumeric(a.type) || !IsNumeric(b.type)) {
		AddDiagnostic(node.id, "演算ノードには数値を接続してください");
		return {};
	}
	const ShaderGraphValueType resultType = ComponentCount(a.type) >= ComponentCount(b.type) ? a.type : b.type;
	a = ConvertExpression(std::move(a), resultType);
	b = ConvertExpression(std::move(b), resultType);
	return ShaderGraphExpression{
		resultType,
		"(" + a.code + " " + std::string(operation) + " " + b.code + ")",
	};
}

// Nodeの用途に応じて式の生成先を選ぶ
ShaderGraphExpression ShaderGraphExpressionCompiler::EmitNodeExpression(const ShaderGraphNode& node, uint32_t outputSlot) {

	// Nodeの用途ごとに生成処理を分ける
	switch (node.kind) {
	case ShaderGraphNodeKind::Parameter:
	case ShaderGraphNodeKind::Constant:
	case ShaderGraphNodeKind::Keyword:
	case ShaderGraphNodeKind::UV:
	case ShaderGraphNodeKind::WorldNormal:
	case ShaderGraphNodeKind::WorldPosition:
	case ShaderGraphNodeKind::ObjectPosition:
	case ShaderGraphNodeKind::ObjectNormal:
	case ShaderGraphNodeKind::ObjectTangent:
	case ShaderGraphNodeKind::ViewDirection:
	case ShaderGraphNodeKind::ScreenPosition:
	case ShaderGraphNodeKind::VertexColor:
	case ShaderGraphNodeKind::Time:
		return EmitValueNode(node, outputSlot);
	case ShaderGraphNodeKind::Add:
	case ShaderGraphNodeKind::Subtract:
	case ShaderGraphNodeKind::Multiply:
	case ShaderGraphNodeKind::Divide:
	case ShaderGraphNodeKind::Power:
	case ShaderGraphNodeKind::Minimum:
	case ShaderGraphNodeKind::Maximum:
	case ShaderGraphNodeKind::Dot:
	case ShaderGraphNodeKind::Cross:
	case ShaderGraphNodeKind::Distance:
	case ShaderGraphNodeKind::Reflect:
	case ShaderGraphNodeKind::Lerp:
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
	case ShaderGraphNodeKind::Length:
	case ShaderGraphNodeKind::Clamp:
	case ShaderGraphNodeKind::Smoothstep:
	case ShaderGraphNodeKind::Step:
	case ShaderGraphNodeKind::Branch:
	case ShaderGraphNodeKind::Remap:
		return EmitMathNode(node);
	case ShaderGraphNodeKind::TilingAndOffset:
	case ShaderGraphNodeKind::PolarCoordinates:
	case ShaderGraphNodeKind::Rotate:
	case ShaderGraphNodeKind::Fresnel:
	case ShaderGraphNodeKind::Dither:
	case ShaderGraphNodeKind::SimpleNoise:
	case ShaderGraphNodeKind::Voronoi:
	case ShaderGraphNodeKind::Split:
	case ShaderGraphNodeKind::Combine:
		return EmitCoordinatesNode(node, outputSlot);
	case ShaderGraphNodeKind::TextureSample:
	case ShaderGraphNodeKind::SamplerState:
	case ShaderGraphNodeKind::NormalUnpack:
		return EmitTextureNode(node, outputSlot);
	case ShaderGraphNodeKind::SceneColor:
	case ShaderGraphNodeKind::SceneMaterial:
	case ShaderGraphNodeKind::SceneEmissive:
	case ShaderGraphNodeKind::SceneDepth:
	case ShaderGraphNodeKind::SceneFlags:
	case ShaderGraphNodeKind::SceneNormal:
	case ShaderGraphNodeKind::ScenePosition:
	case ShaderGraphNodeKind::RayTrace:
		return EmitSceneNode(node, outputSlot);
	case ShaderGraphNodeKind::CustomFunction:
	case ShaderGraphNodeKind::SubGraph:
	case ShaderGraphNodeKind::SurfaceOutput:
	case ShaderGraphNodeKind::UnlitOutput:
	case ShaderGraphNodeKind::PostProcessOutput:
	case ShaderGraphNodeKind::RayTracingOutput:
	case ShaderGraphNodeKind::VertexOutput:
		return EmitCustomNode(node, outputSlot);
	}
	return {};
}
