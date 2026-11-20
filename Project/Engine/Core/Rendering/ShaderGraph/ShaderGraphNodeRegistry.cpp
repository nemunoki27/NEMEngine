#include "ShaderGraphNodeRegistry.h"

//============================================================================
//	include
//============================================================================
// c++
#include <array>
#include <algorithm>
#include <vector>

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
	constexpr std::array kDecalInputs{
		Port{ "Base Color", Type::Float4 }, Port{ "Normal", Type::Float3 },
		Port{ "Metallic", Type::Float }, Port{ "Roughness", Type::Float },
		Port{ "Emission", Type::Float3 }, Port{ "Opacity", Type::Float },
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
		Port{ "Transmission", Type::Float },
		Port{ "Transmission Color", Type::Float3 },
		Port{ "Refraction", Type::Boolean },
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
	constexpr std::array kParticleOutputs{
		Port{ "Color", Type::Float4 }, Port{ "Velocity", Type::Float3 },
		Port{ "Age", Type::Float }, Port{ "Lifetime", Type::Float },
		Port{ "Normalized Age", Type::Float }, Port{ "Stable Random", Type::Float },
		Port{ "CustomData1", Type::Float4 }, Port{ "CustomData2", Type::Float4 },
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
	constexpr std::array kDitherInputs{
		Port{ "In", Type::Invalid }, Port{ "Screen Position", Type::Float4 },
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
		Node{ Kind::DecalOutput, "Decal Surface", "Output", Stage::Fragment, Ports(kDecalInputs), EmptyPorts(), false, false },
		Node{ Kind::SurfaceOutput, "PBR Surface", "Output", Stage::Fragment, Ports(kSurfaceInputs), EmptyPorts(), false, false },
		Node{ Kind::UnlitOutput, "Unlit Surface", "Output", Stage::Fragment, Ports(kUnlitInputs), EmptyPorts(), false, false },
		Node{ Kind::PostProcessOutput, "Post Process", "Output", Stage::Compute, Ports(kPostProcessInputs), EmptyPorts(), false, false },
		Node{ Kind::RayTracingOutput, "Ray Tracing Effect", "Output", Stage::RayGeneration, Ports(kPostProcessInputs), EmptyPorts(), false, false },
		Node{ Kind::VertexOutput, "Vertex", "Output", Stage::Vertex, Ports(kVertexInputs), EmptyPorts(), false, false },
		Node{ Kind::Parameter, "Parameter", "Input", Stage::Any, EmptyPorts(), Ports(kValueOutput), true },
		Node{ Kind::Constant, "Constant", "Input", Stage::Any, EmptyPorts(), Ports(kValueOutput), true },
		Node{ Kind::Keyword, "Keyword", "Input", Stage::Any, EmptyPorts(), Ports(kValueOutput), true },
		Node{ Kind::UV, "UV", "Input", Stage::Any, EmptyPorts(), Ports(kFloat2Output) },
		Node{ Kind::WorldNormal, "World Normal", "Input", Stage::Any, EmptyPorts(), Ports(kFloat3Output), false, true, true },
		Node{ Kind::WorldPosition, "World Position", "Input", Stage::Any, EmptyPorts(), Ports(kFloat3Output), false, true, true },
		Node{ Kind::ObjectPosition, "Object Position", "Input", Stage::Any, EmptyPorts(), Ports(kFloat3Output), false, true, true },
		Node{ Kind::ObjectNormal, "Object Normal", "Input", Stage::Any, EmptyPorts(), Ports(kFloat3Output), false, true, true },
		Node{ Kind::ObjectTangent, "Object Tangent", "Input", Stage::Any, EmptyPorts(), Ports(kFloat3Output), false, true, true },
		Node{ Kind::ViewDirection, "View Direction", "Input", Stage::Fragment, EmptyPorts(), Ports(kFloat3Output), false, true, true },
		Node{ Kind::ScreenPosition, "Screen Position", "Input", Stage::Fragment, EmptyPorts(), Ports(kFloat4Output) },
		Node{ Kind::ParticleData, "Particle Data", "Input", Stage::Fragment, EmptyPorts(), Ports(kParticleOutputs) },
		Node{ Kind::VertexColor, "Vertex Color", "Input", Stage::Any, EmptyPorts(), Ports(kFloat4Output) },
		Node{ Kind::Time, "Time", "Input", Stage::Any, EmptyPorts(), Ports(kTimeOutputs) },
		Node{ Kind::Add, "Add", "Math/Basic", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Subtract, "Subtract", "Math/Basic", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Multiply, "Multiply", "Math/Basic", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Divide, "Divide", "Math/Basic", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Power, "Power", "Math/Advanced", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Minimum, "Minimum", "Math/Basic", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Maximum, "Maximum", "Math/Basic", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Dot, "Dot Product", "Vector", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Cross, "Cross Product", "Vector", Stage::Any, Ports(kBinaryInputs), Ports(kFloat3Output) },
		Node{ Kind::Distance, "Distance", "Vector", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Reflect, "Reflect", "Vector", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Lerp, "Lerp", "Math/Interpolation", Stage::Any, Ports(kLerpInputs), Ports(kValueOutput) },
		Node{ Kind::Clamp, "Clamp", "Math/Range", Stage::Any, Ports(kClampInputs), Ports(kValueOutput) },
		Node{ Kind::Smoothstep, "Smoothstep", "Math/Interpolation", Stage::Any, Ports(kSmoothstepInputs), Ports(kValueOutput) },
		Node{ Kind::Step, "Step", "Math/Range", Stage::Any, Ports(kBinaryInputs), Ports(kValueOutput) },
		Node{ Kind::Branch, "Branch", "Logic", Stage::Any, Ports(kBranchInputs), Ports(kValueOutput) },
		Node{ Kind::OneMinus, "One Minus", "Math/Basic", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Saturate, "Saturate", "Math/Range", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Sine, "Sine", "Math/Trigonometry", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Cosine, "Cosine", "Math/Trigonometry", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Absolute, "Absolute", "Math/Basic", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Floor, "Floor", "Math/Rounding", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Fraction, "Fraction", "Math/Rounding", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::SquareRoot, "Square Root", "Math/Advanced", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Negate, "Negate", "Math/Basic", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Normalize, "Normalize", "Vector", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Length, "Length", "Vector", Stage::Any, Ports(kUnaryInput), Ports(kValueOutput) },
		Node{ Kind::Remap, "Remap", "Math/Range", Stage::Any, Ports(kRemapInputs), Ports(kValueOutput) },
		Node{ Kind::TilingAndOffset, "Tiling And Offset", "UV", Stage::Any, Ports(kTilingInputs), Ports(kFloat2Output) },
		Node{ Kind::PolarCoordinates, "Polar Coordinates", "UV", Stage::Any, Ports(kPolarInputs), Ports(kFloat2Output) },
		Node{ Kind::Rotate, "Rotate", "UV", Stage::Any, Ports(kRotateInputs), Ports(kFloat2Output) },
		Node{ Kind::Split, "Split", "Channel", Stage::Any, Ports(kUnaryInput), Ports(kSplitOutputs) },
		Node{ Kind::Combine, "Combine", "Channel", Stage::Any, Ports(kCombineInputs), Ports(kCombineOutputs) },
		Node{ Kind::TextureSample, "Sample Texture 2D", "Texture", Stage::Any, Ports(kTextureInputs), Ports(kTextureOutputs) },
		Node{ Kind::SamplerState, "Sampler State", "Texture", Stage::Any, EmptyPorts(), Ports(kSamplerOutput), true, false },
		Node{ Kind::NormalUnpack, "Unpack Normal", "Texture", Stage::Fragment, Ports(kUnaryInput), Ports(kFloat3Output), false, true, true },
		Node{ Kind::SceneColor, "Scene Color", "Scene", Stage::Any, Ports(kFloat2Output), Ports(kTextureOutputs) },
		Node{ Kind::SceneDepth, "Scene Depth", "Scene", Stage::Any, Ports(kFloat2Output), Ports(kFloatOutput) },
		Node{ Kind::SceneNormal, "Scene Normal", "Scene", Stage::Any, Ports(kFloat2Output), Ports(kFloat3Output) },
		Node{ Kind::ScenePosition, "Scene Position", "Scene", Stage::Any, Ports(kFloat2Output), Ports(kFloat3Output) },
		Node{ Kind::SceneMaterial, "Scene Material", "Scene", Stage::Any, Ports(kFloat2Output), Ports(kTextureOutputs) },
		Node{ Kind::SceneEmissive, "Scene Emissive", "Scene", Stage::Any, Ports(kFloat2Output), Ports(kTextureOutputs) },
		Node{ Kind::SceneFlags, "Scene Flags", "Scene", Stage::Any, Ports(kFloat2Output), Ports(kFloatOutput) },
		Node{ Kind::RayTrace, "Trace Scene", "Ray Tracing", Stage::RayGeneration, Ports(kRayTraceInputs), Ports(kRayTraceOutputs), false, false },
		Node{ Kind::Fresnel, "Fresnel Effect", "Input", Stage::Fragment, Ports(kFresnelInputs), Ports(kValueOutput), false, true, true },
		Node{ Kind::SimpleNoise, "Simple Noise", "Procedural", Stage::Any, Ports(kNoiseInputs), Ports(kValueOutput) },
		Node{ Kind::Dither, "Dither", "Procedural", Stage::Fragment, Ports(kDitherInputs), Ports(kValueOutput) },
		Node{ Kind::Voronoi, "Voronoi", "Procedural", Stage::Any, Ports(kVoronoiInputs), Ports(kVoronoiOutputs) },
		Node{ Kind::SubGraph, "Sub Graph", "Graph", Stage::Any, EmptyPorts(), EmptyPorts(), true },
		Node{ Kind::CustomFunction, "Custom Function", "Graph", Stage::Any, EmptyPorts(), EmptyPorts(), true },
	};
}

//============================================================================
//	ShaderGraphNodeRegistry classMethods
//============================================================================
std::span<const Engine::ShaderGraphNodeDescriptor>
Engine::ShaderGraphNodeRegistry::GetDescriptors() {

	static const std::vector<ShaderGraphNodeDescriptor> descriptors = [] {

		std::vector<ShaderGraphNodeDescriptor> result(kDescriptors.begin(), kDescriptors.end());
		const auto generic = GetGenericShaderGraphNodeDescriptors();
		result.insert(result.end(), generic.begin(), generic.end());
		return result;
	}();
	return descriptors;
}

const Engine::ShaderGraphNodeDescriptor*
Engine::ShaderGraphNodeRegistry::Find(ShaderGraphNodeKind kind) {

	const auto descriptors = GetDescriptors();
	const auto found = std::find_if(
		descriptors.begin(), descriptors.end(),
		[kind](const ShaderGraphNodeDescriptor& descriptor) {
			return descriptor.kind == kind;
		});
	return found != descriptors.end() ? &*found : nullptr;
}
