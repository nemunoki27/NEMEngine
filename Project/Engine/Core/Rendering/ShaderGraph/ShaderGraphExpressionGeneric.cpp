#include "ShaderGraphExpressionCompiler.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

std::vector<ShaderGraphExpression> ShaderGraphExpressionCompiler::EmitNumericInputs(
	const ShaderGraphNode& node, uint32_t count) {

	std::vector<ShaderGraphExpression> inputs;
	uint32_t width = 1;
	for (uint32_t i = 0; i < count; ++i) {
		ShaderGraphExpression value = EmitDynamicInput(node, i, {});
		if (!IsNumeric(value.type)) {
			AddDiagnostic(node.id, "演算ノードには数値を接続してください");
			return {};
		}
		width = std::max(width, ComponentCount(value.type));
		inputs.emplace_back(std::move(value));
	}
	const auto type = width == 4 ? ShaderGraphValueType::Float4 :
		(width == 3 ? ShaderGraphValueType::Float3 : (width == 2 ? ShaderGraphValueType::Float2 : ShaderGraphValueType::Float));
	for (ShaderGraphExpression& value : inputs) { value = ConvertExpression(std::move(value), type); }
	return inputs;
}

std::string ShaderGraphExpressionCompiler::EmitInputCode(const ShaderGraphNode& node, uint32_t slot,
	ShaderGraphValueType type) {

	return EmitInput(node, slot, type, {}).code;
}

ShaderGraphExpression ShaderGraphExpressionCompiler::EmitGenericNode(const ShaderGraphNode& node, uint32_t outputSlot) {

	switch (node.kind) {
	case ShaderGraphNodeKind::Exponential:
	case ShaderGraphNodeKind::Log:
	case ShaderGraphNodeKind::Modulo:
	case ShaderGraphNodeKind::Posterize:
	case ShaderGraphNodeKind::Reciprocal:
	case ShaderGraphNodeKind::ReciprocalSquareRoot:
	case ShaderGraphNodeKind::DDX:
	case ShaderGraphNodeKind::DDXY:
	case ShaderGraphNodeKind::DDY:
	case ShaderGraphNodeKind::InverseLerp:
	case ShaderGraphNodeKind::RandomRange:
	case ShaderGraphNodeKind::Ceiling:
	case ShaderGraphNodeKind::Round:
	case ShaderGraphNodeKind::Sign:
	case ShaderGraphNodeKind::Truncate:
	case ShaderGraphNodeKind::Arccosine:
	case ShaderGraphNodeKind::Arcsine:
	case ShaderGraphNodeKind::Arctangent:
	case ShaderGraphNodeKind::Arctangent2:
	case ShaderGraphNodeKind::DegreesToRadians:
	case ShaderGraphNodeKind::HyperbolicCosine:
	case ShaderGraphNodeKind::HyperbolicSine:
	case ShaderGraphNodeKind::HyperbolicTangent:
	case ShaderGraphNodeKind::RadiansToDegrees:
	case ShaderGraphNodeKind::Tangent:
	case ShaderGraphNodeKind::Projection:
	case ShaderGraphNodeKind::Rejection:
	case ShaderGraphNodeKind::RotateAboutAxis:
	case ShaderGraphNodeKind::SphereMask:
	case ShaderGraphNodeKind::NoiseSineWave:
	case ShaderGraphNodeKind::SawtoothWave:
	case ShaderGraphNodeKind::SquareWave:
	case ShaderGraphNodeKind::TriangleWave:
	case ShaderGraphNodeKind::Preview:
	case ShaderGraphNodeKind::All:
	case ShaderGraphNodeKind::And:
	case ShaderGraphNodeKind::Any:
	case ShaderGraphNodeKind::Comparison:
	case ShaderGraphNodeKind::IsInfinite:
	case ShaderGraphNodeKind::IsNaN:
	case ShaderGraphNodeKind::Nand:
	case ShaderGraphNodeKind::Not:
	case ShaderGraphNodeKind::Or:
		return EmitGenericMathNode(node);
	case ShaderGraphNodeKind::ChannelMixer:
	case ShaderGraphNodeKind::Contrast:
	case ShaderGraphNodeKind::Hue:
	case ShaderGraphNodeKind::InvertColors:
	case ShaderGraphNodeKind::ReplaceColor:
	case ShaderGraphNodeKind::Saturation:
	case ShaderGraphNodeKind::WhiteBalance:
	case ShaderGraphNodeKind::Blend:
	case ShaderGraphNodeKind::ChannelMask:
	case ShaderGraphNodeKind::ColorMask:
	case ShaderGraphNodeKind::NormalBlend:
	case ShaderGraphNodeKind::NormalFromHeight:
	case ShaderGraphNodeKind::NormalStrength:
	case ShaderGraphNodeKind::ColorspaceConversion:
	case ShaderGraphNodeKind::Flip:
	case ShaderGraphNodeKind::Swizzle:
		return EmitGenericColorNode(node);
	case ShaderGraphNodeKind::Matrix2:
	case ShaderGraphNodeKind::Matrix3:
	case ShaderGraphNodeKind::Matrix4:
	case ShaderGraphNodeKind::MatrixConstruction:
	case ShaderGraphNodeKind::MatrixDeterminant:
	case ShaderGraphNodeKind::MatrixSplit:
	case ShaderGraphNodeKind::MatrixTranspose:
	case ShaderGraphNodeKind::TransformationMatrix:
	case ShaderGraphNodeKind::Transform:
		return EmitGenericMatrixNode(node, outputSlot);
	case ShaderGraphNodeKind::BitangentVector:
	case ShaderGraphNodeKind::Blackbody:
	case ShaderGraphNodeKind::Gradient:
	case ShaderGraphNodeKind::SampleGradient:
	case ShaderGraphNodeKind::DielectricSpecular:
	case ShaderGraphNodeKind::MetalReflectance:
	case ShaderGraphNodeKind::Camera:
	case ShaderGraphNodeKind::Object:
	case ShaderGraphNodeKind::Screen:
	case ShaderGraphNodeKind::VertexID:
	case ShaderGraphNodeKind::IsFrontFace:
		return EmitGenericInputNode(node, outputSlot);
	case ShaderGraphNodeKind::TextureSize:
	case ShaderGraphNodeKind::TextureSampleLOD:
	case ShaderGraphNodeKind::Flipbook:
	case ShaderGraphNodeKind::RadialShear:
	case ShaderGraphNodeKind::Spherize:
	case ShaderGraphNodeKind::Triplanar:
	case ShaderGraphNodeKind::ParallaxMapping:
	case ShaderGraphNodeKind::ParallaxOcclusionMapping:
	case ShaderGraphNodeKind::Twirl:
		return EmitGenericUVNode(node, outputSlot);
	case ShaderGraphNodeKind::Checkerboard:
	case ShaderGraphNodeKind::GradientNoise:
	case ShaderGraphNodeKind::Ellipse:
	case ShaderGraphNodeKind::Polygon:
	case ShaderGraphNodeKind::RoundedPolygon:
	case ShaderGraphNodeKind::RoundedRectangle:
	case ShaderGraphNodeKind::Rectangle:
		return EmitGenericProceduralNode(node);
	default:
		AddDiagnostic(node.id, "ノードの評価処理が登録されていません");
		return {};
	}
}
