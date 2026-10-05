#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================

// c++

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

// Textureの参照とサンプル式を作る
ShaderGraphExpression ShaderGraphExpressionCompiler::EmitTextureNode(const ShaderGraphNode& node, uint32_t outputSlot) {

	switch (node.kind) {
	case ShaderGraphNodeKind::TextureSample: {
		auto sampleFound = textureSampleVariables_.find(node.id.value);
		if (sampleFound == textureSampleVariables_.end()) {

			ShaderGraphExpression texture = EmitInput(node, 0, ShaderGraphValueType::Texture2D, "kNoTexture");
			ShaderGraphExpression uv = EmitInput(node, 1, ShaderGraphValueType::Float2, "graphInput.uv");
			ShaderGraphExpression sampler = EmitInput(node, 2, ShaderGraphValueType::SamplerState, "gSampler");
			const std::string variable = MakeNodeVariable("sample", node.id);
			evaluationStatements_ += "\tconst float4 " + variable + " = SampleGraphTexture(" + texture.code + ", " + uv.code +
									 ", " + sampler.code + ", " + MakeLiteral(node.value, ShaderGraphValueType::Float4) +
									 ");\n";
			sampleFound = textureSampleVariables_.emplace(node.id.value, variable).first;
		}
		const std::string& sample = sampleFound->second;
		switch (outputSlot) {
		case 0:
			return {ShaderGraphValueType::Float4, sample};
		case 1:
			return {ShaderGraphValueType::Float3, "(" + sample + ").rgb"};
		case 2:
			return {ShaderGraphValueType::Float, "(" + sample + ").r"};
		case 3:
			return {ShaderGraphValueType::Float, "(" + sample + ").g"};
		case 4:
			return {ShaderGraphValueType::Float, "(" + sample + ").b"};
		case 5:
			return {ShaderGraphValueType::Float, "(" + sample + ").a"};
		default:
			AddDiagnostic(node.id, "TextureSampleの出力ピンが不正です");
			return {};
		}
	}
	case ShaderGraphNodeKind::SamplerState: {
		const auto found = samplerNames_.find(node.id.value);
		if (found == samplerNames_.end() || outputSlot != 0) {
			AddDiagnostic(node.id, "Sampler Stateの出力ピンが不正です");
			return {};
		}
		return {
			ShaderGraphValueType::SamplerState,
			found->second,
		};
	}
	case ShaderGraphNodeKind::NormalUnpack: {
		ShaderGraphExpression input = EmitInput(node, 0, ShaderGraphValueType::Float4, "float4(0.5f, 0.5f, 1.0f, 1.0f)");
		return ShaderGraphExpression{
			ShaderGraphValueType::Float3,
			"normalize(mul(((" + input.code + ").xyz * 2.0f - 1.0f), graphInput.tangentToWorld))",
		};
	}
	default:
		return {};
	}
}
