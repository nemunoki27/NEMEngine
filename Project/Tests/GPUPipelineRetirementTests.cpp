#include "GPUPipelineRetirementTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineBuilder.h>

// c++
#include <filesystem>
#include <fstream>
// directX
#include <d3dcompiler.h>
#include <Externals/DirectX12/d3dx12.h>

#pragma comment(lib, "d3dcompiler.lib")

bool NEMTests::RecordPipelineOwnerRetirement(ID3D12Device* device, ID3D12GraphicsCommandList6* commands,
	Engine::GraphicsResourceRetirement& retirement, ComPtr<ID3D12Resource>& readback) {

	const char shader[] = "cbuffer Params : register(b0) { uint value; }; "
		"RWStructuredBuffer<uint> result : register(u0); [numthreads(1,1,1)] void main() { result[0] = value; }";
	ComPtr<ID3DBlob> code, errors, signature;
	if (FAILED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "main", "cs_5_0",
		0, 0, &code, &errors))) return false;
	CD3DX12_ROOT_PARAMETER parameters[2];
	parameters[0].InitAsConstantBufferView(0);
	parameters[1].InitAsUnorderedAccessView(0);
	const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(2, parameters);
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors))) return false;
	ComPtr<ID3D12RootSignature> root;
	ComPtr<ID3D12PipelineState> pipeline;
	if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
		IID_PPV_ARGS(&root)))) return false;
	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = root.Get();
	pipelineDesc.CS = { code->GetBufferPointer(), code->GetBufferSize() };
	if (FAILED(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)))) return false;
	Engine::DxConstBuffer<uint32_t> constants;
	constants.CreateBuffer(device);
	constants.TransferData(73);
	ComPtr<ID3D12Resource> output;
	const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	const auto desc = CD3DX12_RESOURCE_DESC::Buffer(4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)))) return false;
	DxUtils::CreateReadbackBufferResource(device, readback, 4);
	commands->SetComputeRootSignature(root.Get());
	commands->SetPipelineState(pipeline.Get());
	commands->SetComputeRootConstantBufferView(0, constants.GetResource()->GetGPUVirtualAddress());
	commands->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
	commands->Dispatch(1, 1, 1);
	const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	commands->ResourceBarrier(1, &barrier);
	commands->CopyBufferRegion(readback.Get(), 0, output.Get(), 0, 4);
	retirement.Retire(ComPtr<ID3D12Resource>(constants.GetResource()));
	retirement.Retire(std::move(output));
	retirement.Retire(std::move(pipeline));
	retirement.Retire(std::move(root));
	const char* shaderPath = "PipelineRetirement.cs.hlsl";
	{
		std::ofstream file(shaderPath);
		file << "[numthreads(1,1,1)] void main() {}";
		if (!file) return false;
	}
	Engine::DxShaderCompiler compiler;
	compiler.Init();
	ComPtr<ID3D12Device8> device8;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device8)))) return false;
	Engine::ComputePipelineDesc engineDesc{};
	engineDesc.compute = { .file = shaderPath, .entry = "main", .profile = "cs_6_0" };
	auto owned = Engine::PipelineStateBuilder::CreateCompute(device8.Get(), &compiler, engineDesc);
	std::filesystem::remove(shaderPath);
	if (!owned) return false;
	owned->SetRetirementQueue(retirement);
	commands->SetComputeRootSignature(owned->GetRootSignature());
	commands->SetPipelineState(owned->GetComputePipeline());
	commands->Dispatch(1, 1, 1);
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 support{};
	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &support, sizeof(support)))) return false;
	if (support.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
		const char* libraryPath = "PipelineRetirement.lib.hlsl";
		{
			std::ofstream file(libraryPath);
			file << "struct Payload { uint value; }; [shader(\"raygeneration\")] void RayGeneration() {} "
				"[shader(\"miss\")] void Miss(inout Payload payload) {}";
			if (!file) return false;
		}
		Engine::ShaderAsset shaderAsset{};
		for (const char* entry : { "RayGeneration", "Miss" }) {
			shaderAsset.stages.push_back({ .stage = Engine::ShaderStage::Lib,
				.file = libraryPath, .entry = entry, .profile = "lib_6_3" });
		}
		Engine::PipelineVariantDesc variant{};
		variant.kind = Engine::PipelineVariantKind::Raytracing;
		variant.rayGenerationExports = { "RayGeneration" };
		variant.missExports = { "Miss" };
		auto rayPipeline = Engine::RaytracingPipelineBuilder::Create(device8.Get(), &compiler, variant, shaderAsset);
		std::filesystem::remove(libraryPath);
		if (!rayPipeline) return false;
		rayPipeline->SetRetirementQueue(retirement);
		commands->SetComputeRootSignature(rayPipeline->GetRootSignature());
		commands->SetPipelineState1(rayPipeline->GetStateObject());
		const auto dispatch = rayPipeline->BuildDispatchDesc(1, 1);
		commands->DispatchRays(&dispatch);
	}
	return true;
}
