#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

ShaderGraphExpression ShaderGraphExpressionCompiler::EmitGenericUVNode(const ShaderGraphNode& node, uint32_t outputSlot) {

	using Kind = ShaderGraphNodeKind;
	using Type = ShaderGraphValueType;
	const auto input = [&](uint32_t slot, Type type) {
		return EmitInputCode(node, slot, type);
	};
	switch (node.kind) {
	case Kind::TextureSize: {
		const auto texture = input(0, Type::Texture2D);
		const auto size = "ShaderGraphTextureSize(" + texture + ")";
		return { Type::Float, "(" + size + ")." + std::string(1, "xyzw"[outputSlot]) };
	}
	case Kind::TextureSampleLOD: {
		auto found = textureSampleVariables_.find(node.id.value);
		if (found == textureSampleVariables_.end()) {
			const auto texture = input(0, Type::Texture2D);
			const auto uv = input(1, Type::Float2);
			const auto sampler = input(2, Type::SamplerState);
			const auto lod = input(3, Type::Float);
			const auto variable = MakeNodeVariable("sampleLOD", node.id);
			evaluationStatements_ += "\tfloat4 " + variable + " = SampleGraphTextureLOD(" + texture + ", " + uv +
				", " + sampler + ", " + lod + ", float4(1, 1, 1, 1));\n";
			found = textureSampleVariables_.emplace(node.id.value, variable).first;
		}
		const auto& sample = found->second;
		if (outputSlot == 0) { return { Type::Float4, sample }; }
		if (outputSlot == 1) { return { Type::Float3, sample + ".rgb" }; }
		return { Type::Float, sample + "." + std::string(1, "rgba"[outputSlot - 2]) };
	}
	case Kind::Flipbook: {
		const auto uv = input(0, Type::Float2);
		const auto width = input(1, Type::Float);
		const auto height = input(2, Type::Float);
		const auto tile = input(3, Type::Float);
		const auto invert = input(4, Type::Float2);
		return { Type::Float2, "ShaderGraphFlipbook(" + uv + ", " + width + ", " + height + ", " + tile + ", " + invert + ")" };
	}
	case Kind::RadialShear:
	case Kind::Spherize:
	case Kind::Twirl: {
		const auto uv = input(0, Type::Float2);
		const auto center = input(1, Type::Float2);
		const auto strength = input(2, node.kind == Kind::RadialShear ? Type::Float2 : Type::Float);
		const auto offset = input(3, Type::Float2);
		const std::string function = node.kind == Kind::RadialShear ? "ShaderGraphRadialShear" :
			(node.kind == Kind::Twirl ? "ShaderGraphTwirl" : "ShaderGraphSpherize");
		return { Type::Float2, function + "(" + uv + ", " + center + ", " + strength + ", " + offset + ")" };
	}
	case Kind::Triplanar: {
		const auto texture = input(0, Type::Texture2D);
		const auto sampler = input(1, Type::SamplerState);
		const auto position = input(2, Type::Float3);
		const auto normal = input(3, Type::Float3);
		const auto tile = input(4, Type::Float);
		const auto blend = input(5, Type::Float);
		return { Type::Float4, "ShaderGraphTriplanar(" + texture + ", " + sampler + ", " + position + ", " + normal +
			", " + tile + ", " + blend + ", " + (node.settings.normalMap ? "true" : "false") + ", graphInput.tangentToWorld)" };
	}
	case Kind::ParallaxMapping: {
		const auto texture = input(0, Type::Texture2D);
		const auto sampler = input(1, Type::SamplerState);
		const auto amplitude = input(2, Type::Float);
		const auto uv = input(3, Type::Float2);
		return { Type::Float2, "ShaderGraphParallaxMapping(" + texture + ", " + sampler + ", " + amplitude + ", " + uv +
			", mul(graphInput.viewDirection, transpose(graphInput.tangentToWorld)))" };
	}
	case Kind::ParallaxOcclusionMapping: {
		const auto texture = input(0, Type::Texture2D);
		const auto sampler = input(1, Type::SamplerState);
		const auto amplitude = input(2, Type::Float);
		const auto steps = input(3, Type::Float);
		const auto uv = input(4, Type::Float2);
		const auto tiling = input(5, Type::Float2);
		const auto offset = input(6, Type::Float2);
		const auto primitive = input(7, Type::Float2);
		const auto lod = input(8, Type::Float);
		const auto threshold = input(9, Type::Float);
		const auto expression = "ShaderGraphParallaxOcclusion(" + texture + ", " + sampler + ", " + amplitude + ", " +
			steps + ", " + uv + ", " + tiling + ", " + offset + ", " + primitive + ", " + lod + ", " + threshold +
			", mul(graphInput.viewDirection, transpose(graphInput.tangentToWorld)))";
		return outputSlot == 0 ? ShaderGraphExpression{ Type::Float, "(" + expression + ").z" } :
			ShaderGraphExpression{ Type::Float2, "(" + expression + ").xy" };
	}
	default: return {};
	}
}
