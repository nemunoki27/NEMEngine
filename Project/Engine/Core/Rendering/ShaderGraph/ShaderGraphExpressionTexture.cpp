#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

bool ShaderGraphExpressionCompiler::UsesMaterialUV(const ShaderGraphNode& node, uint32_t slot, std::string_view uv) const {

	// アトラスや画面座標を混ぜたUVにはタイル変換を掛けない
	std::unordered_set<uint64_t> visited;
	bool materialUV = uv.find("graphInput.uv") != std::string_view::npos;
	const auto walk = [&](auto&& self, const ShaderGraphNode& owner, uint32_t input) -> bool {
		const auto it = incoming_.find({owner.id.value, input});
		if (it == incoming_.end()) {
			if (input < owner.inputPorts.size() && owner.inputPorts[input].hasDefault) return true;
			const auto binding = GetShaderGraphInputDefault(owner, input).binding;
			if (binding == "graphInput.uv") materialUV = true;
			return binding.find("screenPosition") == std::string_view::npos;
		}
		const auto* source = FindNode(it->second->outputNode);
		if (!source || !visited.insert(source->id.value).second) return true;
		using Kind = ShaderGraphNodeKind;
		switch (source->kind) {
		case Kind::UV: materialUV = true; return true;
		case Kind::ScreenPosition:
		case Kind::Flipbook: return false;
		case Kind::TextureSample:
		case Kind::TextureSampleLOD: return true;
		default: break;
		}
		for (uint32_t i = 0; i < GetShaderGraphInputCount(*source); ++i) {
			if (!self(self, *source, i)) return false;
		}
		return true;
	};
	return walk(walk, node, slot) && materialUV;
}

bool ShaderGraphExpressionCompiler::UsesRepeatSampler(const ShaderGraphNode& node, uint32_t slot) const {

	const auto it = incoming_.find({node.id.value, slot});
	const auto* sampling = it == incoming_.end() ? nullptr : FindNode(it->second->outputNode);
	return !sampling || (sampling->sampler.addressU == D3D12_TEXTURE_ADDRESS_MODE_WRAP &&
		sampling->sampler.addressV == D3D12_TEXTURE_ADDRESS_MODE_WRAP);
}

std::string ShaderGraphExpressionCompiler::EmitSurfaceTextureSample(const ShaderGraphNode& node, const std::string& texture,
	const std::string& uv, const std::string& sampler, const std::string& fallback, const std::string& lod) {

	const auto inputNode = [&](uint32_t slot) -> const ShaderGraphNode* {
		const auto it = incoming_.find({node.id.value, slot});
		return it == incoming_.end() ? nullptr : FindNode(it->second->outputNode);
	};
	const bool wrap = UsesRepeatSampler(node, 2);
	const std::string args = ", " + sampler + ", " + (lod.empty() ? "" : lod + ", ") + fallback;
	if (!SupportsHexTiling(graph_) || !wrap || !UsesMaterialUV(node, 1, uv)) {
		return std::string(lod.empty() ? "SampleGraphTexture(" : "SampleGraphTextureLOD(") + texture + ", " + uv + args + ")";
	}

	auto [grid, inserted] = hexGrids_.try_emplace(uv, MakeNodeVariable("hexGrid", node.id));
	if (inserted) evaluationStatements_ += "\tHexTilingData " + grid->second + " = BuildHexTiling(" + uv + ", graphInput.hexTiling);\n";
	const auto* textureNode = inputNode(0);
	const auto param = textureNode ? parameters_.find(textureNode->parameterID.value) : parameters_.end();
	bool normal = param != parameters_.end() && param->second->semantic == MaterialParameterSemantic::NormalTexture;
	for (const auto& link : graph_.links) {
		if (link.outputNode != node.id) continue;
		const auto* destination = FindNode(link.inputNode);
		if (destination && destination->kind == ShaderGraphNodeKind::NormalUnpack) normal = true;
	}
	return std::string(lod.empty() ? "SampleGraphHexTexture(" : "SampleGraphHexTextureLOD(") +
		texture + ", " + grid->second + args + (normal ? ", true)" : ", false)");
}

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
			const auto sample = EmitSurfaceTextureSample(node, texture.code, uv.code, sampler.code,
				MakeLiteral(node.value, ShaderGraphValueType::Float4));
			evaluationStatements_ += "\tconst float4 " + variable + " = " + sample + ";\n";
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
