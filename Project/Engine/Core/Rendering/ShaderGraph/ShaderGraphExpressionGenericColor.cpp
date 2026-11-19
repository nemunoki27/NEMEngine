#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

ShaderGraphExpression ShaderGraphExpressionCompiler::EmitGenericColorNode(const ShaderGraphNode& node) {

	using Kind = ShaderGraphNodeKind;
	using Type = ShaderGraphValueType;
	const auto input = [&](uint32_t slot, Type type) {
		return EmitInputCode(node, slot, type);
	};
	switch (node.kind) {
	case Kind::ChannelMixer: {
		const auto color = input(0, Type::Float3);
		const auto red = input(1, Type::Float3);
		const auto green = input(2, Type::Float3);
		const auto blue = input(3, Type::Float3);
		return { Type::Float3, "float3(dot(" + color + ", " + red + "), dot(" + color + ", " + green +
			"), dot(" + color + ", " + blue + "))" };
	}
	case Kind::Contrast: {
		const auto color = input(0, Type::Float3);
		const auto contrast = input(1, Type::Float);
		return { Type::Float3, "((" + color + " - 0.21763764f) * " + contrast + " + 0.21763764f)" };
	}
	case Kind::Hue: {
		const auto color = input(0, Type::Float3);
		std::string offset = input(1, Type::Float);
		if (node.settings.degrees) { offset = "(" + offset + " / 360.0f)"; }
		return { Type::Float3, "ShaderGraphHue(" + color + ", " + offset + ")" };
	}
	case Kind::ReplaceColor:
	case Kind::ColorMask: {
		const auto color = input(0, Type::Float3);
		const auto from = input(1, Type::Float3);
		const uint32_t offset = node.kind == Kind::ReplaceColor ? 1u : 0u;
		const auto range = input(2u + offset, Type::Float);
		const auto fuzz = input(3u + offset, Type::Float);
		const auto mask = "saturate((distance(" + color + ", " + from + ") - " + range + ") / max(" + fuzz + ", 0.00001f))";
		if (node.kind == Kind::ColorMask) { return { Type::Float, "(1.0f - " + mask + ")" }; }
		const auto to = input(2, Type::Float3);
		return { Type::Float3, "lerp(" + to + ", " + color + ", " + mask + ")" };
	}
	case Kind::Saturation: {
		const auto color = input(0, Type::Float3);
		const auto saturation = input(1, Type::Float);
		return { Type::Float3, "lerp(dot(" + color + ", float3(0.2126729, 0.7151522, 0.0721750)).xxx, " +
			color + ", " + saturation + ")" };
	}
	case Kind::WhiteBalance: {
		const auto color = input(0, Type::Float3);
		return { Type::Float3, "ShaderGraphWhiteBalance(" + color + ", " + input(1, Type::Float) +
			", " + input(2, Type::Float) + ")" };
	}
	case Kind::Blend: {
		auto values = EmitNumericInputs(node, 2);
		if (values.empty()) { return {}; }
		const auto a = ConvertExpression(values[0], Type::Float4);
		const auto b = ConvertExpression(values[1], Type::Float4);
		const auto opacity = input(2, Type::Float);
		return ConvertExpression({ Type::Float4, "ShaderGraphBlend(" + a.code + ", " + b.code + ", " +
			opacity + ", " + std::to_string(static_cast<uint32_t>(node.settings.blend)) + "u)" }, values[0].type);
	}
	case Kind::InvertColors:
	case Kind::ChannelMask:
	case Kind::Flip: {
		auto values = EmitNumericInputs(node, 1);
		if (values.empty()) { return {}; }
		const auto& value = values[0];
		const uint32_t width = ComponentCount(value.type);
		std::string mask = HLSLType(value.type) + "(";
		for (uint32_t i = 0; i < width; ++i) {
			if (i != 0) { mask += ", "; }
			mask += (node.settings.channelMask & (1u << i)) != 0 ? "1.0f" : "0.0f";
		}
		mask += ")";
		if (node.kind == Kind::InvertColors) { return { value.type, "lerp(" + value.code + ", 1.0f - " + value.code + ", " + mask + ")" }; }
		if (node.kind == Kind::Flip) { return { value.type, "(" + value.code + " * (1.0f - 2.0f * " + mask + "))" }; }
		return { value.type, "(" + value.code + " * " + mask + ")" };
	}
	case Kind::Swizzle: {
		const auto value = input(0, Type::Float4);
		std::string swizzle;
		for (const uint32_t channel : node.settings.swizzle) { swizzle += "xyzw"[std::min(channel, 3u)]; }
		return { Type::Float4, "(" + value + ")." + swizzle };
	}
	case Kind::NormalBlend: {
		const auto a = input(0, Type::Float3);
		const auto b = input(1, Type::Float3);
		if (node.settings.normalBlend == ShaderGraphNormalBlend::Reoriented) {
			return { Type::Float3, "ShaderGraphNormalBlendReoriented(" + a + ", " + b + ")" };
		}
		return { Type::Float3, "normalize(float3((" + a + ").xy + (" + b + ").xy, (" + a + ").z * (" + b + ").z))" };
	}
	case Kind::NormalStrength: {
		const auto a = input(0, Type::Float3);
		const auto strength = input(1, Type::Float);
		return { Type::Float3, "float3((" + a + ").xy * " + strength + ", lerp(1.0f, (" + a + ").z, saturate(" + strength + ")))" };
	}
	case Kind::NormalFromHeight: {
		const auto height = input(0, Type::Float);
		const auto strength = input(1, Type::Float);
		const auto result = "ShaderGraphNormalFromHeight(" + height + ", " + strength +
			", graphInput.worldPosition, graphInput.worldNormal, graphInput.tangentToWorld)";
		return { Type::Float3, node.settings.normalSpace == ShaderGraphNormalSpace::World ?
			"mul(" + result + ", graphInput.tangentToWorld)" : result };
	}
	case Kind::ColorspaceConversion: {
		std::string color = input(0, Type::Float3);
		if (node.settings.colorFrom == node.settings.colorTo) { return { Type::Float3, color }; }
		if (node.settings.colorFrom == ShaderGraphColorSpace::HSV) { color = "ShaderGraphHSVToRGB(" + color + ")"; }
		if (node.settings.colorFrom == ShaderGraphColorSpace::Linear) { color = "ShaderGraphLinearToSRGB(" + color + ")"; }
		if (node.settings.colorTo == ShaderGraphColorSpace::HSV) { color = "ShaderGraphRGBToHSV(" + color + ")"; }
		if (node.settings.colorTo == ShaderGraphColorSpace::Linear) { color = "ShaderGraphSRGBToLinear(" + color + ")"; }
		return { Type::Float3, color };
	}
	default: return {};
	}
}
