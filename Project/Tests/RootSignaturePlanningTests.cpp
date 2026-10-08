#include "TestContracts.h"
#include "GPUBufferLifetimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Stage/RootSignaturePlanning.h>
#include <Engine/Core/Rendering/Pipelines/Stage/AutoRootSignatureBuilder.h>

// c++
#include <string>
#include <vector>

namespace {

	// バイナリを実行せずreflectionの配置だけを検証する
	Engine::CompiledShader MakeShader(Engine::ShaderStage stage) {

		Engine::CompiledShader shader{};
		shader.stage = stage;
		shader.bytecode = {1};
		return shader;
	}
	Engine::ShaderResourceBinding MakeBinding(const std::string& name, Engine::ShaderBindingKind kind,
		D3D_SHADER_INPUT_TYPE rawType, UINT bindPoint, UINT space, UINT count, Engine::ShaderStage stage) {

		Engine::ShaderResourceBinding binding{};
		binding.name = name;
		binding.kind = kind;
		binding.rawType = rawType;
		binding.bindPoint = bindPoint;
		binding.space = space;
		binding.bindCount = count;
		binding.stageMask = stage;
		return binding;
	}
	Engine::CompiledShader MakeSamplerShader(Engine::ShaderStage stage) {

		auto shader = MakeShader(stage);
		shader.reflection.resources.push_back(MakeBinding("Sampler", Engine::ShaderBindingKind::Sampler,
			D3D_SIT_SAMPLER, 0, 0, 1, stage));
		return shader;
	}
	D3D12_STATIC_SAMPLER_DESC MakeStaticSampler(D3D12_SHADER_VISIBILITY visibility) {

		D3D12_STATIC_SAMPLER_DESC sampler{};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.MaxAnisotropy = 1;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderVisibility = visibility;
		return sampler;
	}

	bool CheckSamplerVisibility() {

		using namespace Engine;
		auto vertex = MakeSamplerShader(ShaderStage::VS);
		auto pixel = MakeSamplerShader(ShaderStage::PS);
		std::vector<D3D12_STATIC_SAMPLER_DESC> samplers{MakeStaticSampler(D3D12_SHADER_VISIBILITY_PIXEL)};
		auto plan = RootSignaturePlanning::Build(PipelineType::Vertex, {&pixel}, samplers);
		if (!plan.IsValid() || !plan.bindings.empty()) {
			return false;
		}
		// 同じRegisterでもVertex側には動的Samplerを残す
		plan = RootSignaturePlanning::Build(PipelineType::Vertex, {&vertex, &pixel}, samplers);
		if (!plan.IsValid() || plan.bindings.size() != 1 || plan.visibilities.front() != D3D12_SHADER_VISIBILITY_VERTEX ||
			plan.bindings.front().parameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
			return false;
		}
		samplers.push_back(MakeStaticSampler(D3D12_SHADER_VISIBILITY_VERTEX));
		plan = RootSignaturePlanning::Build(PipelineType::Vertex, {&vertex, &pixel}, samplers);
		if (!plan.IsValid() || !plan.bindings.empty()) {
			return false;
		}
		// ALLの動的入力と一部ステージだけの静的入力を重ねない
		vertex.reflection.resources.front().stageMask = ShaderStage::VS | ShaderStage::PS;
		if (RootSignaturePlanning::Build(PipelineType::Vertex, {&vertex}, samplers).IsValid()) {
			return false;
		}
		samplers = {MakeStaticSampler(D3D12_SHADER_VISIBILITY_ALL)};
		plan = RootSignaturePlanning::Build(PipelineType::Vertex, {&vertex}, samplers);
		auto compute = MakeSamplerShader(ShaderStage::CS);
		return plan.IsValid() && plan.bindings.empty() &&
			!RootSignaturePlanning::Build(PipelineType::Compute, {&compute},
				{MakeStaticSampler(D3D12_SHADER_VISIBILITY_PIXEL)}).IsValid();
	}

	bool CheckLayoutAndOwnership() {

		using namespace Engine;
		auto vertex = MakeShader(ShaderStage::VS);
		auto pixel = MakeShader(ShaderStage::PS);
		vertex.reflection.resources = {
			MakeBinding("Structured", ShaderBindingKind::SRV, D3D_SIT_STRUCTURED, 3, 1, 1, ShaderStage::VS),
			MakeBinding("Constants", ShaderBindingKind::CBV, D3D_SIT_CBUFFER, 2, 0, 1, ShaderStage::VS),
			MakeBinding("SharedCube", ShaderBindingKind::SRV, D3D_SIT_TEXTURE, 0, 7, 0, ShaderStage::VS)};
		pixel.reflection.resources = {
			MakeBinding("SharedCube", ShaderBindingKind::SRV, D3D_SIT_TEXTURE, 0, 7, 1, ShaderStage::PS),
			MakeBinding("Image", ShaderBindingKind::SRV, D3D_SIT_TEXTURE, 8, 1, 1, ShaderStage::PS),
			MakeBinding("Samplers", ShaderBindingKind::Sampler, D3D_SIT_SAMPLER, 2, 0, 2, ShaderStage::PS)};
		pixel.reflection.requiresFlags = D3D_SHADER_REQUIRES_RESOURCE_DESCRIPTOR_HEAP_INDEXING;
		std::vector<D3D12_STATIC_SAMPLER_DESC> samplers(2);
		samplers[0].ShaderRegister = 2;
		samplers[1].ShaderRegister = 3;
		auto plan = RootSignaturePlanning::Build(PipelineType::Vertex, {&vertex, &pixel}, samplers);
		if (!plan.IsValid() || plan.bindings.size() != 4 || plan.staticSamplers.size() != 2 ||
			plan.bindings[0].name != "Constants" || plan.bindings[1].name != "Structured" ||
			plan.bindings[2].name != "Image" || plan.bindings[3].name != "SharedCube" || plan.bindings[3].bindCount != 0 ||
			plan.bindings[0].parameterType != D3D12_ROOT_PARAMETER_TYPE_CBV ||
			plan.bindings[1].parameterType != D3D12_ROOT_PARAMETER_TYPE_SRV ||
			plan.bindings[2].parameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ||
			plan.visibilities[0] != D3D12_SHADER_VISIBILITY_VERTEX || plan.visibilities[2] != D3D12_SHADER_VISIBILITY_PIXEL ||
			plan.visibilities[3] != D3D12_SHADER_VISIBILITY_ALL ||
			(plan.flags & D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT) == 0 ||
			(plan.flags & D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED) == 0) {

			return false;
		}
		for (size_t index = 0; index < plan.bindings.size(); ++index) {
			if (plan.bindings[index].rootParameterIndex != index) {
				return false;
			}
		}
		// 元のShaderとSamplerの変更を持ち越さない
		vertex.reflection.resources.clear();
		pixel.reflection.resources.clear();
		samplers[0].ShaderRegister = 9;
		if (plan.bindings[3].name != "SharedCube" || plan.staticSamplers[0].ShaderRegister != 2) {
			return false;
		}
		auto compute = MakeShader(ShaderStage::CS);
		compute.reflection.resources.push_back(MakeBinding("Input", ShaderBindingKind::SRV,
			D3D_SIT_STRUCTURED, 0, 0, 1, ShaderStage::CS));
		plan = RootSignaturePlanning::Build(PipelineType::Compute, {&compute}, {});
		return plan.IsValid() && plan.visibilities.front() == D3D12_SHADER_VISIBILITY_ALL &&
			(plan.flags & D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT) == 0;
	}

	bool CheckInvalidBindings() {

		using namespace Engine;
		auto first = MakeShader(ShaderStage::VS);
		auto second = MakeShader(ShaderStage::PS);
		first.reflection.resources.push_back(MakeBinding("Array", ShaderBindingKind::SRV,
			D3D_SIT_TEXTURE, 0, 0, 2, ShaderStage::VS));
		second.reflection.resources = first.reflection.resources;
		second.reflection.resources[0].bindCount = 3;
		if (RootSignaturePlanning::Build(PipelineType::Vertex, {&first, &second}, {}).IsValid()) {
			return false;
		}
		// 配置失敗ならDeviceへ触れず空の結果を返す
		AutoRootSignatureBuilder builder;
		auto rejected = builder.Build(nullptr, PipelineType::Vertex, {&first, &second});
		if (rejected.rootSignature || !rejected.bindings.empty()) {
			return false;
		}
		second.reflection.resources[0].bindCount = 2;
		second.reflection.resources[0].rawType = D3D_SIT_STRUCTURED;
		if (RootSignaturePlanning::Build(PipelineType::Vertex, {&first, &second}, {}).IsValid()) {
			return false;
		}
		first.reflection.resources[0].kind = static_cast<ShaderBindingKind>(99);
		if (RootSignaturePlanning::Build(PipelineType::Vertex, {&first}, {}).IsValid()) {
			return false;
		}
		CompiledShader empty;
		auto plan = RootSignaturePlanning::Build(PipelineType::Vertex, {nullptr, &empty}, {});
		return plan.IsValid() && plan.bindings.empty();
	}

	bool CheckSamplersAndRootCost() {

		using namespace Engine;
		auto shader = MakeShader(ShaderStage::CS);
		shader.reflection.resources.push_back(MakeBinding("Samplers", ShaderBindingKind::Sampler,
			D3D_SIT_SAMPLER, 2, 0, 2, ShaderStage::CS));
		std::vector<D3D12_STATIC_SAMPLER_DESC> samplers(2);
		samplers[0].ShaderRegister = 2;
		samplers[1].ShaderRegister = 3;
		if (!RootSignaturePlanning::Build(PipelineType::Compute, {&shader}, samplers).IsValid()) {
			return false;
		}
		samplers.pop_back();
		if (RootSignaturePlanning::Build(PipelineType::Compute, {&shader}, samplers).IsValid()) {
			return false;
		}
		shader.reflection.resources[0].bindCount = 0;
		if (RootSignaturePlanning::Build(PipelineType::Compute, {&shader}, samplers).IsValid() ||
			!RootSignaturePlanning::Build(PipelineType::Compute, {&shader}, {}).IsValid()) {
			return false;
		}
		// 上限ちょうどは通し、1つ追加したら拒否する
		shader.reflection.resources.clear();
		for (UINT index = 0; index < D3D12_MAX_ROOT_COST / 2; ++index) {
			shader.reflection.resources.push_back(MakeBinding("Constant" + std::to_string(index),
				ShaderBindingKind::CBV, D3D_SIT_CBUFFER, index, 0, 1, ShaderStage::CS));
		}
		if (!RootSignaturePlanning::Build(PipelineType::Compute, {&shader}, {}).IsValid()) {
			return false;
		}
		shader.reflection.resources.push_back(MakeBinding("Overflow", ShaderBindingKind::CBV,
			D3D_SIT_CBUFFER, D3D12_MAX_ROOT_COST / 2, 0, 1, ShaderStage::CS));
		return !RootSignaturePlanning::Build(PipelineType::Compute, {&shader}, {}).IsValid();
	}
}

bool NEMTests::TestRootSignaturePlanning() {

	return CheckLayoutAndOwnership() && CheckInvalidBindings() && CheckSamplersAndRootCost() && CheckSamplerVisibility();
}

bool NEMTests::CheckRootSignaturePlanning(ID3D12Device* device) {

	using namespace Engine;
	auto shader = MakeShader(ShaderStage::CS);
	shader.reflection.resources = {
		MakeBinding("Constants", ShaderBindingKind::CBV, D3D_SIT_CBUFFER, 0, 0, 1, ShaderStage::CS),
		MakeBinding("Images", ShaderBindingKind::SRV, D3D_SIT_TEXTURE, 0, 1, 0, ShaderStage::CS)};
	AutoRootSignatureBuilder builder;
	auto result = builder.Build(device, PipelineType::Compute, {&shader});
	if (!result.rootSignature || result.bindings.size() != 2 || result.bindings[1].bindCount != 0 ||
		result.bindings[1].parameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
		return false;
	}
	// シリアライズの失敗でもDevice呼出しへ進まない
	std::vector<D3D12_STATIC_SAMPLER_DESC> duplicates(2, MakeStaticSampler(D3D12_SHADER_VISIBILITY_ALL));
	auto rejected = builder.Build(nullptr, PipelineType::Compute, {&shader}, duplicates);
	if (rejected.rootSignature || !rejected.bindings.empty() || !result.rootSignature) {
		return false;
	}
	// 同一Registerの静的Pixel・動的Vertex配置を実生成する
	auto vertex = MakeSamplerShader(ShaderStage::VS);
	auto pixel = MakeSamplerShader(ShaderStage::PS);
	auto split = builder.Build(device, PipelineType::Vertex, {&vertex, &pixel},
		{MakeStaticSampler(D3D12_SHADER_VISIBILITY_PIXEL)});
	if (!split.rootSignature || split.bindings.size() != 1 || split.bindings.front().stageMask != ShaderStage::VS) {
		return false;
	}
	auto fullyStatic = builder.Build(device, PipelineType::Vertex, {&vertex, &pixel},
		{MakeStaticSampler(D3D12_SHADER_VISIBILITY_VERTEX), MakeStaticSampler(D3D12_SHADER_VISIBILITY_PIXEL)});
	return fullyStatic.rootSignature && fullyStatic.bindings.empty();
}
