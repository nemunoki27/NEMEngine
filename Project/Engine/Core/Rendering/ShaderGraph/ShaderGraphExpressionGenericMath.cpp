#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================
// c++
#include <array>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

ShaderGraphExpression ShaderGraphExpressionCompiler::EmitGenericMathNode(const ShaderGraphNode& node) {

	using Kind = ShaderGraphNodeKind;
	using Type = ShaderGraphValueType;
	switch (node.kind) {
	case Kind::Reciprocal:
	case Kind::ReciprocalSquareRoot:
	case Kind::DDX:
	case Kind::DDXY:
	case Kind::DDY:
	case Kind::Ceiling:
	case Kind::Round:
	case Kind::Sign:
	case Kind::Truncate:
	case Kind::Arccosine:
	case Kind::Arcsine:
	case Kind::Arctangent:
	case Kind::DegreesToRadians:
	case Kind::HyperbolicCosine:
	case Kind::HyperbolicSine:
	case Kind::HyperbolicTangent:
	case Kind::RadiansToDegrees:
	case Kind::Tangent:
	case Kind::All:
	case Kind::Any:
	case Kind::IsInfinite:
	case Kind::IsNaN:
	case Kind::Preview:
	case Kind::SquareWave:
	case Kind::TriangleWave:
	case Kind::SawtoothWave:
	case Kind::Exponential:
	case Kind::Log:
	{
		auto values = EmitNumericInputs(node, 1);
		if (values.empty()) { return {}; }
		const auto& a = values[0];
		switch (node.kind) {
		case Kind::Reciprocal: return { a.type, "rcp(" + a.code + ")" };
		case Kind::ReciprocalSquareRoot: return { a.type, "rsqrt(" + a.code + ")" };
		case Kind::DDX: return { a.type, "ddx(" + a.code + ")" };
		case Kind::DDXY: return { a.type, "fwidth(" + a.code + ")" };
		case Kind::DDY: return { a.type, "ddy(" + a.code + ")" };
		case Kind::Ceiling: return { a.type, "ceil(" + a.code + ")" };
		case Kind::Round: return { a.type, "round(" + a.code + ")" };
		case Kind::Sign: return { a.type, "sign(" + a.code + ")" };
		case Kind::Truncate: return { a.type, "trunc(" + a.code + ")" };
		case Kind::Arccosine: return { a.type, "acos(" + a.code + ")" };
		case Kind::Arcsine: return { a.type, "asin(" + a.code + ")" };
		case Kind::Arctangent: return { a.type, "atan(" + a.code + ")" };
		case Kind::DegreesToRadians: return { a.type, "radians(" + a.code + ")" };
		case Kind::HyperbolicCosine: return { a.type, "cosh(" + a.code + ")" };
		case Kind::HyperbolicSine: return { a.type, "sinh(" + a.code + ")" };
		case Kind::HyperbolicTangent: return { a.type, "tanh(" + a.code + ")" };
		case Kind::RadiansToDegrees: return { a.type, "degrees(" + a.code + ")" };
		case Kind::Tangent: return { a.type, "tan(" + a.code + ")" };
		case Kind::All: return { Type::Boolean, "all(" + a.code + ")" };
		case Kind::Any: return { Type::Boolean, "any(" + a.code + ")" };
		case Kind::IsInfinite: return { Type::Boolean, "any(isinf(" + a.code + "))" };
		case Kind::IsNaN: return { Type::Boolean, "any(isnan(" + a.code + "))" };
		case Kind::Preview: return a;
		case Kind::Exponential: {
			const char* function = node.settings.mathBase == ShaderGraphMathBase::Base2 ? "exp2" : "exp";
			const std::string value = node.settings.mathBase == ShaderGraphMathBase::Base10 ?
				"(" + a.code + " * 2.302585093f)" : a.code;
			return { a.type, std::string(function) + "(" + value + ")" };
		}
		case Kind::Log: {
			const char* function = node.settings.mathBase == ShaderGraphMathBase::Base2 ? "log2" :
				(node.settings.mathBase == ShaderGraphMathBase::Base10 ? "log10" : "log");
			return { a.type, std::string(function) + "(" + a.code + ")" };
		}
		case Kind::SquareWave: return { a.type, "(1.0f - 2.0f * round(frac(" + a.code + ")))" };
		case Kind::TriangleWave:
			return { a.type, "(2.0f * abs(2.0f * (" + a.code + " - floor(0.5f + " + a.code + "))) - 1.0f)" };
		case Kind::SawtoothWave: return { a.type, "(2.0f * (" + a.code + " - floor(0.5f + " + a.code + ")))" };
		default: return {};
		}
	}
	case Kind::Modulo:
	case Kind::Arctangent2:
	case Kind::Posterize:
	case Kind::Projection:
	case Kind::Rejection: {
		auto v = EmitNumericInputs(node, 2);
		if (v.empty()) { return {}; }
		const auto& a = v[0];
		const auto& b = v[1];
		if (node.kind == Kind::Modulo) { return { a.type, "fmod(" + a.code + ", " + b.code + ")" }; }
		if (node.kind == Kind::Arctangent2) { return { a.type, "atan2(" + a.code + ", " + b.code + ")" }; }
		if (node.kind == Kind::Posterize) {
			return { a.type, "(floor(" + a.code + " * " + b.code + ") / " + b.code + ")" };
		}
		const std::string projection = "(" + b.code + " * dot(" + a.code + ", " + b.code + ") / dot(" + b.code + ", " + b.code + "))";
		return { a.type, node.kind == Kind::Projection ? projection : "(" + a.code + " - " + projection + ")" };
	}
	case Kind::InverseLerp: {
		auto v = EmitNumericInputs(node, 3);
		if (v.empty()) { return {}; }
		return { v[0].type, "((" + v[2].code + " - " + v[0].code + ") / (" + v[1].code + " - " + v[0].code + "))" };
	}
	case Kind::RandomRange: {
		const std::string seed = EmitInputCode(node, 0, Type::Float2);
		const std::string low = EmitInputCode(node, 1, Type::Float);
		const std::string high = EmitInputCode(node, 2, Type::Float);
		return { Type::Float, "lerp(" + low + ", " + high + ", frac(sin(dot(" + seed + ", float2(12.9898, 78.233))) * 43758.5453))" };
	}
	case Kind::NoiseSineWave: {
		const auto a = EmitDynamicInput(node, 0, {});
		if (!IsNumeric(a.type)) { AddDiagnostic(node.id, "波形には数値を接続してください"); return {}; }
		const auto range = EmitInputCode(node, 1, Type::Float2);
		const auto random = "frac(sin((sin(" + a.code + ") - sin(" + a.code + " + 1)) * 91.2228) * 43758.5453)";
		return { a.type, "(sin(" + a.code + ") + lerp((" + range + ").x, (" + range + ").y, " + random + "))" };
	}
	case Kind::RotateAboutAxis: {
		const auto a = EmitInputCode(node, 0, Type::Float3);
		const auto axis = EmitInputCode(node, 1, Type::Float3);
		std::string angle = EmitInputCode(node, 2, Type::Float);
		if (node.settings.degrees) { angle = "radians(" + angle + ")"; }
		return { Type::Float3, "ShaderGraphRotateAboutAxis(" + a + ", " + axis + ", " + angle + ")" };
	}
	case Kind::SphereMask: {
		auto v = EmitNumericInputs(node, 2);
		if (v.empty()) { return {}; }
		const auto radius = EmitInputCode(node, 2, Type::Float);
		const auto hardness = EmitInputCode(node, 3, Type::Float);
		return { Type::Float, "(1.0f - saturate((distance(" + v[0].code + ", " + v[1].code + ") - " + radius +
			") / max(1.0f - " + hardness + ", 0.00001f)))" };
	}
	case Kind::Comparison: {
		const auto a = EmitInputCode(node, 0, Type::Float);
		const auto b = EmitInputCode(node, 1, Type::Float);
		static constexpr std::array operations{ "==", "!=", "<", "<=", ">", ">=" };
		return { Type::Boolean, "(" + a + " " + operations[static_cast<size_t>(node.settings.comparison)] + " " + b + ")" };
	}
	case Kind::And:
	case Kind::Nand:
	case Kind::Or:
	case Kind::Not: {
		const auto a = EmitInputCode(node, 0, Type::Boolean);
		if (node.kind == Kind::Not) { return { Type::Boolean, "!(" + a + ")" }; }
		const auto b = EmitInputCode(node, 1, Type::Boolean);
		const std::string expression = "((" + a + ") " + (node.kind == Kind::Or ? "||" : "&&") + " (" + b + "))";
		return { Type::Boolean, node.kind == Kind::Nand ? "!(" + expression + ")" : expression };
	}
	default: return {};
	}
}
