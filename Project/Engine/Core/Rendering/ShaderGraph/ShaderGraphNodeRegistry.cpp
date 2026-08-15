#include "ShaderGraphNodeRegistry.h"

//============================================================================
//	include
//============================================================================
// c++
#include <array>
#include <algorithm>

namespace {

	using Port = Engine::ShaderGraphPortDescriptor;
	using Node = Engine::ShaderGraphNodeDescriptor;
	using Kind = Engine::ShaderGraphNodeKind;
	using Stage = Engine::ShaderGraphStage;
	using Type = Engine::ShaderGraphValueType;

	constexpr std::array kNoPorts{
		Port{ "", Type::Invalid },
	};
	constexpr std::array kValueOutput{
		Port{ "Output", Type::Invalid },
	};
	constexpr std::array kUnaryInput{
		Port{ "Input", Type::Invalid },
	};
	constexpr std::array kBinaryInputs{
		Port{ "A", Type::Invalid }, Port{ "B", Type::Invalid },
	};
	constexpr std::array kSurfaceInputs{
		Port{ "Base Color", Type::Float4 },
		Port{ "Normal", Type::Float3 },
		Port{ "Metallic", Type::Float },
		Port{ "Roughness", Type::Float },
		Port{ "Ambient Occlusion", Type::Float },
		Port{ "Emissive", Type::Float3 },
		Port{ "Opacity", Type::Float },
		Port{ "Alpha Clip", Type::Float },
	};
	constexpr std::array kUnlitInputs{
		Port{ "Color", Type::Float4 },
		Port{ "Opacity", Type::Float },
		Port{ "Alpha Clip", Type::Float },
	};
	constexpr std::array kPostProcessInputs{
		Port{ "Color", Type::Float4 },
	};
	constexpr std::array kRayTraceInputs{
		Port{ "Origin", Type::Float3 }, Port{ "Direction", Type::Float3 },
		Port{ "Min Distance", Type::Float },
		Port{ "Max Distance", Type::Float }, Port{ "Mask", Type::Integer },
	};
	constexpr std::array kRayTraceOutputs{
		Port{ "Color", Type::Float3 }, Port{ "Hit", Type::Float },
		Port{ "Distance", Type::Float }, Port{ "Position", Type::Float3 },
		Port{ "Normal", Type::Float3 },
	};
	constexpr std::array kVertexInputs{
		Port{ "Position (Object)", Type::Float3 },
		Port{ "Normal (Object)", Type::Float3 },
		Port{ "Tangent (Object)", Type::Float3 },
	};
	constexpr std::array kTextureInputs{
		Port{ "Texture", Type::Texture2D }, Port{ "UV", Type::Float2 },
		Port{ "Sampler", Type::SamplerState },
	};
	constexpr std::array kSamplerOutput{
		Port{ "Sampler", Type::SamplerState },
	};
	constexpr std::array kTextureOutputs{
		Port{ "RGBA", Type::Float4 }, Port{ "RGB", Type::Float3 },
		Port{ "R", Type::Float }, Port{ "G", Type::Float },
		Port{ "B", Type::Float }, Port{ "A", Type::Float },
	};
	constexpr std::array kTimeOutputs{
		Port{ "Time", Type::Float }, Port{ "Sine Time", Type::Float },
		Port{ "Cosine Time", Type::Float }, Port{ "Delta Time", Type::Float },
		Port{ "Smooth Delta", Type::Float },
	};
	constexpr std::array kLerpInputs{
		Port{ "A", Type::Invalid }, Port{ "B", Type::Invalid },
		Port{ "T", Type::Float },
	};
	constexpr std::array kRemapInputs{
		Port{ "Input", Type::Invalid }, Port{ "In Min Max", Type::Float2 },
		Port{ "Out Min Max", Type::Float2 },
	};
	constexpr std::array kTilingInputs{
		Port{ "UV", Type::Float2 }, Port{ "Tiling", Type::Float2 },
		Port{ "Offset", Type::Float2 },
	};
	constexpr std::array kPolarInputs{
		Port{ "UV", Type::Float2 }, Port{ "Center", Type::Float2 },
		Port{ "Radial Scale", Type::Float }, Port{ "Length Scale", Type::Float },
	};
	constexpr std::array kSplitOutputs{
		Port{ "R", Type::Float }, Port{ "G", Type::Float },
		Port{ "B", Type::Float }, Port{ "A", Type::Float },
	};
	constexpr std::array kCombineInputs{
		Port{ "R", Type::Float }, Port{ "G", Type::Float },
		Port{ "B", Type::Float }, Port{ "A", Type::Float },
	};
	constexpr std::array kCombineOutputs{
		Port{ "RGBA", Type::Float4 }, Port{ "RGB", Type::Float3 },
		Port{ "RG", Type::Float2 },
	};
	constexpr std::array kClampInputs{
		Port{ "Input", Type::Invalid }, Port{ "Minimum", Type::Invalid },
		Port{ "Maximum", Type::Invalid },
	};
	constexpr std::array kSmoothstepInputs{
		Port{ "Edge 1", Type::Invalid }, Port{ "Edge 2", Type::Invalid },
		Port{ "Input", Type::Invalid },
	};
	constexpr std::array kBranchInputs{
		Port{ "Predicate", Type::Boolean }, Port{ "True", Type::Invalid },
		Port{ "False", Type::Invalid },
	};
	constexpr std::array kFresnelInputs{
		Port{ "Normal", Type::Float3 }, Port{ "View Direction", Type::Float3 },
		Port{ "Power", Type::Float },
	};
	constexpr std::array kRotateInputs{
		Port{ "UV", Type::Float2 }, Port{ "Center", Type::Float2 },
		Port{ "Rotation", Type::Float },
	};
	constexpr std::array kNoiseInputs{
		Port{ "UV", Type::Float2 }, Port{ "Scale", Type::Float },
	};
	constexpr std::array kVoronoiInputs{
		Port{ "UV", Type::Float2 }, Port{ "Angle Offset", Type::Float },
		Port{ "Cell Density", Type::Float },
	};
	constexpr std::array kVoronoiOutputs{
		Port{ "Out", Type::Float }, Port{ "Cells", Type::Float },
	};
	constexpr std::array kFloat2Output{
		Port{ "Output", Type::Float2 },
	};
	constexpr std::array kFloat3Output{
		Port{ "Output", Type::Float3 },
	};
	constexpr std::array kFloat4Output{
		Port{ "Output", Type::Float4 },
	};
	constexpr std::array kFloatOutput{
		Port{ "Output", Type::Float },
	};

	constexpr auto EmptyPorts() {

		return std::span<const Port>{};
	}

	template <size_t Size>
	constexpr auto Ports(const std::array<Port, Size>& ports) {

		return std::span<const Port>{ ports };
	}

	const std::array kDescriptors{
		Node{ Kind::SurfaceOutput, "PBR Surface", "出力", Stage::Fragment, Ports(kSurfaceInputs), EmptyPorts(), false, false },
		Node{ Kind::UnlitOutput, "Unlit Surface", "出力", Stage::Fragment, Ports(kUnlitInputs), EmptyPorts(), false, false },
		Node{ Kind::PostProcessOutput, "Post Process", "出力", Stage::Compute, Ports(kPostProcessInputs), EmptyPorts(), false, false },
		Node{ Kind::RayTracingOutput, "Ray Tracing Effect", "出力", Stage::RayGeneration, Ports(kPostProcessInputs), EmptyPorts(), false, false },
		Node{ Kind::VertexOutput, "Vertex", "出力", Stage::Vertex, Ports(kVertexInputs), EmptyPorts(), false, false },
		Node{ Kind::Parameter, "Parameter", "入力", Stage::Any, EmptyPorts(), Ports(kValueOutput), true, false },
		Node{ Kind::Constant, "Constant", "入力", Stage::Any, EmptyPorts(), Ports(kValueOutput), true, false },
		Node{ Kind::Keyword, "Keyword", "入力", Stage::Any, EmptyPorts(), Ports(kValueOutput), true, false },
		Node{ Kind::UV, "UV", "入力", Stage::Any, EmptyPorts(), Ports(kFloat2Output) },
		Node{ Kind::WorldNormal, "World Normal", "入力", Stage::Any, EmptyPorts(), Ports(kFloat3Output) },
		Node{ Kind::WorldPosition, "World Position", "入力", Stage::Any, EmptyPorts(), Ports(kFloat3Output) },
		Node{ Kind::ObjectPosition, "Object Position", "入力", Stage::Any, EmptyPorts(), Ports(kFloat3Output) },
		Node{ Kind::ObjectNormal, "Object Normal", "入力", Stage::Any, EmptyPorts(), Ports(kFloat3Output) },
		Node{ Kind::ObjectTangent, "Object Tangent", "入力", Stage::Any, EmptyPorts(), Ports(kFloat3Output) },
		Node{ Kind::ViewDirection, "View Direction", "入力", Stage::Fragment, EmptyPorts(), Ports(kFloat3Output) },
		Node{ Kind::ScreenPosition, "Screen Position", "入力", Stage::Fragment, EmptyPorts(), Ports(kFloat4Output) },
		Node{ Kind::VertexColor, "Vertex Color", "入力", Stage::Any, EmptyPorts(), Ports(kFloat4Output) },
		Node{ Kind::Time, "Time", "入力", Stage::Any, EmptyPorts(), Ports(kTimeOutputs) },
		Node{ Kind::Add, "Add", "演算", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Subtract, "Subtract", "演算", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Multiply, "Multiply", "演算", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Divide, "Divide", "演算", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Power, "Power", "演算", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Minimum, "Minimum", "演算", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Maximum, "Maximum", "演算", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Dot, "Dot Product", "ベクトル", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Cross, "Cross Product", "ベクトル", Stage::Any, Ports(kBinaryInputs), Ports(kFloat3Output) },
		Node{ Kind::Distance, "Distance", "ベクトル", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Reflect, "Reflect", "ベクトル", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Lerp, "Lerp", "演算", Stage::Any, Ports(kLerpInputs), Ports(kValueOutput) },
		Node{ Kind::Clamp, "Clamp", "演算", Stage::Any, Ports(kClampInputs), Ports(kValueOutput) },
		Node{ Kind::Smoothstep, "Smoothstep", "演算", Stage::Any, Ports(kSmoothstepInputs), Ports(kValueOutput) },
		Node{ Kind::Step, "Step", "演算", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Branch, "Branch", "ロジック", Stage::Any, Ports(kBranchInputs), Ports(kValueOutput) },
		Node{ Kind::OneMinus, "One Minus", "演算", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Saturate, "Saturate", "演算", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Sine, "Sine", "演算", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Cosine, "Cosine", "演算", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Absolute, "Absolute", "演算", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Floor, "Floor", "演算", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Fraction, "Fraction", "演算", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::SquareRoot, "Square Root", "演算", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Negate, "Negate", "演算", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Normalize, "Normalize", "ベクトル", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Length, "Length", "ベクトル", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Remap, "Remap", "演算", Stage::Any, Ports(kRemapInputs), Ports(kValueOutput) },
		Node{ Kind::TilingAndOffset, "Tiling And Offset", "UV", Stage::Any, Ports(kTilingInputs), Ports(kFloat2Output) },
		Node{ Kind::PolarCoordinates, "Polar Coordinates", "UV", Stage::Any, Ports(kPolarInputs), Ports(kFloat2Output) },
		Node{ Kind::Rotate, "Rotate", "UV", Stage::Any, Ports(kRotateInputs), Ports(kFloat2Output) },
		Node{ Kind::Split, "Split", "チャンネル", Stage::Any, Ports(kUnaryInput), Ports(kSplitOutputs) },
		Node{ Kind::Combine, "Combine", "チャンネル", Stage::Any, Ports(kCombineInputs), Ports(kCombineOutputs) },
		Node{ Kind::TextureSample, "Sample Texture 2D", "テクスチャ", Stage::Any, Ports(kTextureInputs), Ports(kTextureOutputs) },
		Node{ Kind::SamplerState, "Sampler State", "テクスチャ", Stage::Any, EmptyPorts(), Ports(kSamplerOutput), true, false },
		Node{ Kind::NormalUnpack, "Unpack Normal", "テクスチャ", Stage::Fragment, Ports(kUnaryInput), Ports(kFloat3Output) },
		Node{ Kind::SceneColor, "Scene Color", "シーン", Stage::Any, Ports(kFloat2Output), Ports(kTextureOutputs) },
		Node{ Kind::SceneDepth, "Scene Depth", "シーン", Stage::Any, Ports(kFloat2Output), Ports(kFloatOutput) },
		Node{ Kind::SceneNormal, "Scene Normal", "シーン", Stage::Any, Ports(kFloat2Output), Ports(kFloat3Output) },
		Node{ Kind::ScenePosition, "Scene Position", "シーン", Stage::Any, Ports(kFloat2Output), Ports(kFloat3Output) },
		Node{ Kind::SceneMaterial, "Scene Material", "シーン", Stage::Any, Ports(kFloat2Output), Ports(kTextureOutputs) },
		Node{ Kind::SceneEmissive, "Scene Emissive", "シーン", Stage::Any, Ports(kFloat2Output), Ports(kTextureOutputs) },
		Node{ Kind::SceneFlags, "Scene Flags", "シーン", Stage::Any, Ports(kFloat2Output), Ports(kFloatOutput) },
		Node{ Kind::RayTrace, "Trace Scene", "レイトレーシング", Stage::RayGeneration, Ports(kRayTraceInputs), Ports(kRayTraceOutputs), false, false },
		Node{ Kind::Fresnel, "Fresnel Effect", "入力", Stage::Fragment, Ports(kFresnelInputs), Ports(kValueOutput) },
		Node{ Kind::SimpleNoise, "Simple Noise", "プロシージャル", Stage::Any, Ports(kNoiseInputs), Ports(kValueOutput) },
		Node{ Kind::Voronoi, "Voronoi", "プロシージャル", Stage::Any, Ports(kVoronoiInputs), Ports(kVoronoiOutputs) },
		Node{ Kind::SubGraph, "Sub Graph", "グラフ", Stage::Any, EmptyPorts(), EmptyPorts(), true },
		Node{ Kind::CustomFunction, "Custom Function", "グラフ", Stage::Any, EmptyPorts(), EmptyPorts(), true },
	};
}

//============================================================================
//	ShaderGraphNodeRegistry classMethods
//============================================================================
std::span<const Engine::ShaderGraphNodeDescriptor>
Engine::ShaderGraphNodeRegistry::GetDescriptors() {

	return kDescriptors;
}

const Engine::ShaderGraphNodeDescriptor*
Engine::ShaderGraphNodeRegistry::Find(ShaderGraphNodeKind kind) {

	const auto found = std::find_if(
		kDescriptors.begin(), kDescriptors.end(),
		[kind](const ShaderGraphNodeDescriptor& descriptor) {
			return descriptor.kind == kind;
		});
	return found != kDescriptors.end() ? &*found : nullptr;
}
