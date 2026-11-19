#include "ShaderGraphNodeRegistry.h"

//============================================================================
//	include
//============================================================================
// c++
#include <array>

namespace {

	using Port = Engine::ShaderGraphPortDescriptor;
	using Node = Engine::ShaderGraphNodeDescriptor;
	using Kind = Engine::ShaderGraphNodeKind;
	using Type = Engine::ShaderGraphValueType;
	using Stage = Engine::ShaderGraphStage;

	constexpr std::array kPorts0{
		Port{ "In", Type::Invalid },
	};
	constexpr std::array kPorts1{
		Port{ "Out", Type::Invalid },
	};
	constexpr std::array kPorts2{
		Port{ "A", Type::Invalid }, Port{ "B", Type::Invalid },
	};
	constexpr std::array kPorts3{
		Port{ "In", Type::Invalid }, Port{ "Steps", Type::Invalid },
	};
	constexpr std::array kPorts4{
		Port{ "A", Type::Invalid }, Port{ "B", Type::Invalid },
		Port{ "T", Type::Invalid },
	};
	constexpr std::array kPorts5{
		Port{ "In", Type::Invalid }, Port{ "Min Max", Type::Float2 },
	};
	constexpr std::array kPorts6{
		Port{ "Seed", Type::Float2 }, Port{ "Min", Type::Float },
		Port{ "Max", Type::Float },
	};
	constexpr std::array kPorts7{
		Port{ "Out", Type::Float },
	};
	constexpr std::array kPorts8{
		Port{ "In", Type::Float3 }, Port{ "Axis", Type::Float3 },
		Port{ "Rotation", Type::Float },
	};
	constexpr std::array kPorts9{
		Port{ "Out", Type::Float3 },
	};
	constexpr std::array kPorts10{
		Port{ "Coordinates", Type::Invalid }, Port{ "Center", Type::Invalid },
		Port{ "Radius", Type::Float }, Port{ "Hardness", Type::Float },
	};
	constexpr std::array kPorts11{
		Port{ "In", Type::Float3 },
	};
	constexpr std::array kPorts12{
		Port{ "Out", Type::Matrix2 },
	};
	constexpr std::array kPorts13{
		Port{ "Out", Type::Matrix3 },
	};
	constexpr std::array kPorts14{
		Port{ "Out", Type::Matrix4 },
	};
	constexpr std::array kPorts15{
		Port{ "M0", Type::Float4 }, Port{ "M1", Type::Float4 },
		Port{ "M2", Type::Float4 }, Port{ "M3", Type::Float4 },
	};
	constexpr std::array kPorts16{
		Port{ "4x4", Type::Matrix4 }, Port{ "3x3", Type::Matrix3 },
		Port{ "2x2", Type::Matrix2 },
	};
	constexpr std::array kPorts17{
		Port{ "In", Type::Matrix4 },
	};
	constexpr std::array kPorts18{
		Port{ "In", Type::Float3 }, Port{ "Red", Type::Float3 },
		Port{ "Green", Type::Float3 }, Port{ "Blue", Type::Float3 },
	};
	constexpr std::array kPorts19{
		Port{ "In", Type::Float3 }, Port{ "Contrast", Type::Float },
	};
	constexpr std::array kPorts20{
		Port{ "In", Type::Float3 }, Port{ "Offset", Type::Float },
	};
	constexpr std::array kPorts21{
		Port{ "In", Type::Float3 }, Port{ "From", Type::Float3 },
		Port{ "To", Type::Float3 }, Port{ "Range", Type::Float },
		Port{ "Fuzziness", Type::Float },
	};
	constexpr std::array kPorts22{
		Port{ "In", Type::Float3 }, Port{ "Saturation", Type::Float },
	};
	constexpr std::array kPorts23{
		Port{ "In", Type::Float3 }, Port{ "Temperature", Type::Float },
		Port{ "Tint", Type::Float },
	};
	constexpr std::array kPorts24{
		Port{ "Base", Type::Invalid }, Port{ "Blend", Type::Invalid },
		Port{ "Opacity", Type::Float },
	};
	constexpr std::array kPorts25{
		Port{ "In", Type::Float3 }, Port{ "Mask Color", Type::Float3 },
		Port{ "Range", Type::Float }, Port{ "Fuzziness", Type::Float },
	};
	constexpr std::array kPorts26{
		Port{ "A", Type::Float3 }, Port{ "B", Type::Float3 },
	};
	constexpr std::array kPorts27{
		Port{ "In", Type::Float }, Port{ "Strength", Type::Float },
	};
	constexpr std::array kPorts28{
		Port{ "In", Type::Float3 }, Port{ "Strength", Type::Float },
	};
	constexpr std::array kPorts29{
		Port{ "Temperature", Type::Float },
	};
	constexpr std::array kPorts30{
		Port{ "Out", Type::Gradient },
	};
	constexpr std::array kPorts31{
		Port{ "Gradient", Type::Gradient }, Port{ "Time", Type::Float },
	};
	constexpr std::array kPorts32{
		Port{ "Out", Type::Float4 },
	};
	constexpr std::array kPorts33{
		Port{ "Range", Type::Float }, Port{ "IOR", Type::Float },
	};
	constexpr std::array kPorts35{
		Port{ "Position", Type::Float3 }, Port{ "Direction", Type::Float3 },
		Port{ "Orthographic", Type::Float }, Port{ "Near Plane", Type::Float },
		Port{ "Far Plane", Type::Float }, Port{ "Z Buffer Sign", Type::Float },
		Port{ "Width", Type::Float }, Port{ "Height", Type::Float },
	};
	constexpr std::array kPorts36{
		Port{ "Position", Type::Float3 }, Port{ "Scale", Type::Float3 },
		Port{ "World Bounds Min", Type::Float3 }, Port{ "World Bounds Max", Type::Float3 },
		Port{ "Bounds Size", Type::Float3 },
	};
	constexpr std::array kPorts37{
		Port{ "Width", Type::Float }, Port{ "Height", Type::Float },
	};
	constexpr std::array kPorts38{
		Port{ "Out", Type::Integer },
	};
	constexpr std::array kPorts39{
		Port{ "Out", Type::Boolean },
	};
	constexpr std::array kPorts40{
		Port{ "Texture", Type::Texture2D },
	};
	constexpr std::array kPorts41{
		Port{ "Width", Type::Float }, Port{ "Height", Type::Float },
		Port{ "Texel Width", Type::Float }, Port{ "Texel Height", Type::Float },
	};
	constexpr std::array kPorts42{
		Port{ "Texture", Type::Texture2D }, Port{ "UV", Type::Float2 },
		Port{ "Sampler", Type::SamplerState }, Port{ "LOD", Type::Float },
	};
	constexpr std::array kPorts43{
		Port{ "RGBA", Type::Float4 }, Port{ "RGB", Type::Float3 },
		Port{ "R", Type::Float }, Port{ "G", Type::Float },
		Port{ "B", Type::Float }, Port{ "A", Type::Float },
	};
	constexpr std::array kPorts44{
		Port{ "UV", Type::Float2 }, Port{ "Color A", Type::Float3 },
		Port{ "Color B", Type::Float3 }, Port{ "Frequency", Type::Float2 },
	};
	constexpr std::array kPorts45{
		Port{ "UV", Type::Float2 }, Port{ "Scale", Type::Float },
	};
	constexpr std::array kPorts46{
		Port{ "UV", Type::Float2 }, Port{ "Width", Type::Float },
		Port{ "Height", Type::Float },
	};
	constexpr std::array kPorts47{
		Port{ "UV", Type::Float2 }, Port{ "Sides", Type::Float },
		Port{ "Width", Type::Float }, Port{ "Height", Type::Float },
	};
	constexpr std::array kPorts48{
		Port{ "UV", Type::Float2 }, Port{ "Width", Type::Float },
		Port{ "Height", Type::Float }, Port{ "Radius", Type::Float },
	};
	constexpr std::array kPorts49{
		Port{ "UV", Type::Float2 }, Port{ "Width", Type::Float },
		Port{ "Height", Type::Float }, Port{ "Sides", Type::Float },
		Port{ "Roundness", Type::Float },
	};
	constexpr std::array kPorts50{
		Port{ "A", Type::Boolean }, Port{ "B", Type::Boolean },
	};
	constexpr std::array kPorts51{
		Port{ "In", Type::Boolean },
	};
	constexpr std::array kPorts52{
		Port{ "A", Type::Float }, Port{ "B", Type::Float },
	};
	constexpr std::array kPorts53{
		Port{ "UV", Type::Float2 }, Port{ "Width", Type::Float },
		Port{ "Height", Type::Float }, Port{ "Tile", Type::Float },
		Port{ "Invert", Type::Float2 },
	};
	constexpr std::array kPorts54{
		Port{ "Out", Type::Float2 },
	};
	constexpr std::array kPorts55{
		Port{ "UV", Type::Float2 }, Port{ "Center", Type::Float2 },
		Port{ "Strength", Type::Float2 }, Port{ "Offset", Type::Float2 },
	};
	constexpr std::array kPorts56{
		Port{ "UV", Type::Float2 }, Port{ "Center", Type::Float2 },
		Port{ "Strength", Type::Float }, Port{ "Offset", Type::Float2 },
	};
	constexpr std::array kPorts57{
		Port{ "Texture", Type::Texture2D }, Port{ "Sampler", Type::SamplerState },
		Port{ "Position", Type::Float3 }, Port{ "Normal", Type::Float3 },
		Port{ "Tile", Type::Float }, Port{ "Blend", Type::Float },
	};
	constexpr std::array kPorts58{
		Port{ "Heightmap", Type::Texture2D }, Port{ "Sampler", Type::SamplerState },
		Port{ "Amplitude", Type::Float }, Port{ "UV", Type::Float2 },
	};
	constexpr std::array kPorts59{
		Port{ "Heightmap", Type::Texture2D }, Port{ "Sampler", Type::SamplerState },
		Port{ "Amplitude", Type::Float }, Port{ "Steps", Type::Float },
		Port{ "UV", Type::Float2 }, Port{ "Tiling", Type::Float2 },
		Port{ "Offset", Type::Float2 }, Port{ "Primitive Size", Type::Float2 },
		Port{ "LOD", Type::Float }, Port{ "LOD Threshold", Type::Float },
	};
	constexpr std::array kPorts60{
		Port{ "Pixel Depth Offset", Type::Float }, Port{ "Parallax UV", Type::Float2 },
	};

	const std::array kDescriptors{
		Node{ Kind::Exponential, "Exponential", "Math/Advanced", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Log, "Log", "Math/Advanced", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Reciprocal, "Reciprocal", "Math/Basic", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::ReciprocalSquareRoot, "Reciprocal Square Root", "Math/Advanced", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Ceiling, "Ceiling", "Math/Rounding", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Round, "Round", "Math/Rounding", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Sign, "Sign", "Math/Basic", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Truncate, "Truncate", "Math/Rounding", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Arccosine, "Arccosine", "Math/Trigonometry", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Arcsine, "Arcsine", "Math/Trigonometry", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Arctangent, "Arctangent", "Math/Trigonometry", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::DegreesToRadians, "Degrees To Radians", "Math/Trigonometry", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::HyperbolicCosine, "Hyperbolic Cosine", "Math/Trigonometry", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::HyperbolicSine, "Hyperbolic Sine", "Math/Trigonometry", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::HyperbolicTangent, "Hyperbolic Tangent", "Math/Trigonometry", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::RadiansToDegrees, "Radians To Degrees", "Math/Trigonometry", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Tangent, "Tangent", "Math/Trigonometry", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::SawtoothWave, "Sawtooth Wave", "Math/Wave", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::SquareWave, "Square Wave", "Math/Wave", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::TriangleWave, "Triangle Wave", "Math/Wave", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Preview, "Preview", "Graph", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Modulo, "Modulo", "Math/Basic", Stage::Any,
			kPorts2, kPorts1, false, true },
		Node{ Kind::Arctangent2, "Arctangent2", "Math/Trigonometry", Stage::Any,
			kPorts2, kPorts1, false, true },
		Node{ Kind::Posterize, "Posterize", "Math/Range", Stage::Any,
			kPorts3, kPorts1, false, true },
		Node{ Kind::DDX, "DDX", "Derivative", Stage::Fragment,
			kPorts0, kPorts1, false, true },
		Node{ Kind::DDXY, "DDXY", "Derivative", Stage::Fragment,
			kPorts0, kPorts1, false, true },
		Node{ Kind::DDY, "DDY", "Derivative", Stage::Fragment,
			kPorts0, kPorts1, false, true },
		Node{ Kind::InverseLerp, "Inverse Lerp", "Math/Interpolation", Stage::Any,
			kPorts4, kPorts1, false, true },
		Node{ Kind::NoiseSineWave, "Noise Sine Wave", "Math/Wave", Stage::Any,
			kPorts5, kPorts1, false, true },
		Node{ Kind::RandomRange, "Random Range", "Math/Range", Stage::Any,
			kPorts6, kPorts7, false, true },
		Node{ Kind::Projection, "Projection", "Vector", Stage::Any,
			kPorts2, kPorts1, false, true },
		Node{ Kind::Rejection, "Rejection", "Vector", Stage::Any,
			kPorts2, kPorts1, false, true },
		Node{ Kind::RotateAboutAxis, "Rotate About Axis", "Vector", Stage::Any,
			kPorts8, kPorts9, false, true },
		Node{ Kind::SphereMask, "Sphere Mask", "Vector", Stage::Any,
			kPorts10, kPorts7, false, true },
		Node{ Kind::Transform, "Transform", "Vector", Stage::Any,
			kPorts11, kPorts9, false, true, true },
		Node{ Kind::Matrix2, "Matrix 2x2", "Matrix", Stage::Any,
			{}, kPorts12, false, true },
		Node{ Kind::Matrix3, "Matrix 3x3", "Matrix", Stage::Any,
			{}, kPorts13, false, true },
		Node{ Kind::Matrix4, "Matrix 4x4", "Matrix", Stage::Any,
			{}, kPorts14, false, true },
		Node{ Kind::MatrixConstruction, "Matrix Construction", "Matrix", Stage::Any,
			kPorts15, kPorts16, false, true },
		Node{ Kind::MatrixDeterminant, "Matrix Determinant", "Matrix", Stage::Any,
			kPorts17, kPorts7, false, true },
		Node{ Kind::MatrixTranspose, "Matrix Transpose", "Matrix", Stage::Any,
			kPorts17, kPorts14, false, true },
		Node{ Kind::MatrixSplit, "Matrix Split", "Matrix", Stage::Any,
			kPorts17, kPorts15, false, true },
		Node{ Kind::TransformationMatrix, "Transformation Matrix", "Matrix", Stage::Any,
			{}, kPorts14, false, true },
		Node{ Kind::ChannelMixer, "Channel Mixer", "Color", Stage::Any,
			kPorts18, kPorts9, false, true },
		Node{ Kind::Contrast, "Contrast", "Color", Stage::Any,
			kPorts19, kPorts9, false, true },
		Node{ Kind::Hue, "Hue", "Color", Stage::Any,
			kPorts20, kPorts9, false, true },
		Node{ Kind::InvertColors, "Invert Colors", "Channel", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::ChannelMask, "Channel Mask", "Channel", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Flip, "Flip", "Channel", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::Swizzle, "Swizzle", "Channel", Stage::Any,
			kPorts0, kPorts1, false, true },
		Node{ Kind::ReplaceColor, "Replace Color", "Color", Stage::Any,
			kPorts21, kPorts9, false, true },
		Node{ Kind::Saturation, "Saturation", "Color", Stage::Any,
			kPorts22, kPorts9, false, true },
		Node{ Kind::WhiteBalance, "White Balance", "Color", Stage::Any,
			kPorts23, kPorts9, false, true },
		Node{ Kind::Blend, "Blend", "Color", Stage::Any,
			kPorts24, kPorts1, false, true },
		Node{ Kind::ColorMask, "Color Mask", "Color", Stage::Any,
			kPorts25, kPorts7, false, true },
		Node{ Kind::NormalBlend, "Normal Blend", "Normal", Stage::Any,
			kPorts26, kPorts9, false, true, true },
		Node{ Kind::NormalFromHeight, "Normal From Height", "Normal", Stage::Fragment,
			kPorts27, kPorts9, false, true, true },
		Node{ Kind::NormalStrength, "Normal Strength", "Normal", Stage::Any,
			kPorts28, kPorts9, false, true, true },
		Node{ Kind::ColorspaceConversion, "Colorspace Conversion", "Color", Stage::Any,
			kPorts11, kPorts9, false, true },
		Node{ Kind::BitangentVector, "Bitangent Vector", "Input", Stage::Fragment,
			{}, kPorts9, false, true, true },
		Node{ Kind::Blackbody, "Blackbody", "Input", Stage::Any,
			kPorts29, kPorts9, false, true },
		Node{ Kind::Gradient, "Gradient", "Input", Stage::Any,
			{}, kPorts30, false, true },
		Node{ Kind::SampleGradient, "Sample Gradient", "Input", Stage::Any,
			kPorts31, kPorts32, false, true },
		Node{ Kind::DielectricSpecular, "Dielectric Specular", "Input", Stage::Any,
			kPorts33, kPorts7, false, true },
		Node{ Kind::MetalReflectance, "Metal Reflectance", "Input", Stage::Any,
			{}, kPorts9, false, true },
		Node{ Kind::Camera, "Camera", "Scene", Stage::Any,
			{}, kPorts35, false, true },
		Node{ Kind::Object, "Object", "Scene", Stage::Any,
			{}, kPorts36, false, true },
		Node{ Kind::Screen, "Screen", "Scene", Stage::Any,
			{}, kPorts37, false, true },
		Node{ Kind::VertexID, "Vertex ID", "Input", Stage::Vertex,
			{}, kPorts38, false, false },
		Node{ Kind::IsFrontFace, "Is Front Face", "Input", Stage::Fragment,
			{}, kPorts39, false, true },
		Node{ Kind::TextureSize, "Texture Size", "Texture", Stage::Any,
			kPorts40, kPorts41, false, true },
		Node{ Kind::TextureSampleLOD, "Sample Texture 2D LOD", "Texture", Stage::Any,
			kPorts42, kPorts43, false, true },
		Node{ Kind::Checkerboard, "Checkerboard", "Procedural", Stage::Fragment,
			kPorts44, kPorts9, false, true },
		Node{ Kind::GradientNoise, "Gradient Noise", "Procedural", Stage::Any,
			kPorts45, kPorts7, false, true },
		Node{ Kind::Ellipse, "Ellipse", "Procedural", Stage::Fragment,
			kPorts46, kPorts7, false, true },
		Node{ Kind::Rectangle, "Rectangle", "Procedural", Stage::Fragment,
			kPorts46, kPorts7, false, true },
		Node{ Kind::Polygon, "Polygon", "Procedural", Stage::Fragment,
			kPorts47, kPorts7, false, true },
		Node{ Kind::RoundedRectangle, "Rounded Rectangle", "Procedural", Stage::Fragment,
			kPorts48, kPorts7, false, true },
		Node{ Kind::RoundedPolygon, "Rounded Polygon", "Procedural", Stage::Fragment,
			kPorts49, kPorts7, false, true },
		Node{ Kind::All, "All", "Logic", Stage::Any,
			kPorts0, kPorts39, false, true },
		Node{ Kind::Any, "Any", "Logic", Stage::Any,
			kPorts0, kPorts39, false, true },
		Node{ Kind::IsInfinite, "Is Infinite", "Logic", Stage::Any,
			kPorts0, kPorts39, false, true },
		Node{ Kind::IsNaN, "Is Na N", "Logic", Stage::Any,
			kPorts0, kPorts39, false, true },
		Node{ Kind::And, "And", "Logic", Stage::Any,
			kPorts50, kPorts39, false, true },
		Node{ Kind::Nand, "Nand", "Logic", Stage::Any,
			kPorts50, kPorts39, false, true },
		Node{ Kind::Or, "Or", "Logic", Stage::Any,
			kPorts50, kPorts39, false, true },
		Node{ Kind::Not, "Not", "Logic", Stage::Any,
			kPorts51, kPorts39, false, true },
		Node{ Kind::Comparison, "Comparison", "Logic", Stage::Any,
			kPorts52, kPorts39, false, true },
		Node{ Kind::Flipbook, "Flipbook", "UV", Stage::Any,
			kPorts53, kPorts54, false, true },
		Node{ Kind::RadialShear, "Radial Shear", "UV", Stage::Any,
			kPorts55, kPorts54, false, true },
		Node{ Kind::Spherize, "Spherize", "UV", Stage::Any,
			kPorts56, kPorts54, false, true },
		Node{ Kind::Twirl, "Twirl", "UV", Stage::Any,
			kPorts56, kPorts54, false, true },
		Node{ Kind::Triplanar, "Triplanar", "UV", Stage::Fragment,
			kPorts57, kPorts32, false, true, true },
		Node{ Kind::ParallaxMapping, "Parallax Mapping", "UV", Stage::Fragment,
			kPorts58, kPorts54, false, true, true },
		Node{ Kind::ParallaxOcclusionMapping, "Parallax Occlusion Mapping", "UV", Stage::Fragment,
			kPorts59, kPorts60, false, true, true },
	};
}

std::span<const Engine::ShaderGraphNodeDescriptor> Engine::GetGenericShaderGraphNodeDescriptors() {

	return kDescriptors;
}

