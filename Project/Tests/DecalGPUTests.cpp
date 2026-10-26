#include "DecalTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Decals/DecalTypes.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

bool NEMTests::TestDecalGPU() {

	using namespace Engine;
	TestDirectory directory("DecalGPU");
	const auto path = directory.GetPath() / "decalTest.CS.hlsl";
	// 通常描画とDXRが共有する評価処理を実際のGPUで実行する
	std::ofstream source(path);
	source << "#include \"" << Algorithm::PathToUTF8(RuntimePaths::GetEngineAssetPath("Shaders/Builtin/Decal/decalCommon.hlsli")) << "\"\n";
	source << R"(
RWStructuredBuffer<float4> output : register(u0);
SamplerState testSampler : register(s1);
[numthreads(8,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint test = id.x;
    float3 color = 0.4f.xxx, normal = float3(0,0,-1), emissive = 0;
    float metallic = 0.2f, roughness = 0.6f;
    uint geometry = test == 2 ? 1 : (test == 4 ? 2 : (test == 7 ? 99 : 0));
    if (test == 5) normal = float3(0,0,1);
    float3 position = float3(test == 1 ? 1 : (test == 6 ? 0.49f : 0),0,0);
    ApplyRaytracingDecals(geometry, position, 0.001f, test == 3 ? 2 : 1, testSampler,
        color, normal, metallic, roughness, emissive);
    output[test*3] = float4(color, metallic);
    output[test*3+1] = float4(normal, roughness);
    output[test*3+2] = float4(emissive,1);
})";
	source.close();
	DxShaderCompiler compiler;
	compiler.Init();
	const auto shader = compiler.CompileShader(path.wstring(), L"cs_6_0", L"main", ShaderStage::CS);
	if (!shader.IsValid()) {
		std::cerr << shader.diagnostics << '\n';
		return false;
	}
	ComPtr<ID3D12Device> device;
	if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) return false;
	CD3DX12_DESCRIPTOR_RANGE textures;
	textures.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 126);
	CD3DX12_ROOT_PARAMETER parameters[10];
	parameters[0].InitAsConstants(4, 0, 5);
	parameters[1].InitAsShaderResourceView(0, 5);
	parameters[2].InitAsShaderResourceView(1, 5);
	parameters[3].InitAsShaderResourceView(2, 5);
	parameters[4].InitAsUnorderedAccessView(0);
	parameters[5].InitAsDescriptorTable(1, &textures);
	parameters[6].InitAsConstants(4, 3, 4);
	parameters[7].InitAsShaderResourceView(7, 4);
	parameters[8].InitAsShaderResourceView(10, 4);
	parameters[9].InitAsShaderResourceView(11, 4);
	const CD3DX12_STATIC_SAMPLER_DESC sampler(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(10, parameters, 1, &sampler);
	ComPtr<ID3DBlob> bytes, errors;
	ComPtr<ID3D12RootSignature> root;
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &bytes, &errors)) ||
		FAILED(device->CreateRootSignature(0, bytes->GetBufferPointer(), bytes->GetBufferSize(), IID_PPV_ARGS(&root)))) return false;
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = root.Get();
	desc.CS = {shader.GetBytecodePointer(), shader.GetBytecodeSize()};
	ComPtr<ID3D12PipelineState> pipeline;
	const HRESULT pipelineResult = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline));
	if (FAILED(pipelineResult)) {
		std::cerr << "Decal GPU Pipeline creation failed: " << std::hex << pipelineResult << std::dec << '\n';
		return false;
	}
	std::array<DecalGPUData, 2> decals;
	for (auto& decal : decals) {
		decal.direction = Vector4(0, 0, 1, 0);
		decal.tangentX = Vector4(1, 0, 0, 0);
		decal.tangentY = Vector4(0, 1, 0, 0);
		decal.fade = Vector4(0.5f, 0.1f, 0, 0);
		decal.surface = Vector4(1, 0.1f, 1, 0.5f);
		decal.receiverMask = 1;
	}
	decals[0].baseColor = Color4(1, 0, 0, 1);
	decals[0].emissive = Color4(4, 2, 0, 0);
	decals[0].channels = 1 | 4 | 8;
	decals[1].baseColor = Color4(0, 1, 0, 1);
	decals[1].channels = 1;
	const std::array<DecalReceiverRange, 3> ranges{{{0, 1}, {0, 2}, {0, 0}}};
	const uint32_t indices[]{0, 1};
	ComPtr<ID3D12Resource> input, receiver, candidates, output, readback;
	const auto upload = [&](auto& resource, const auto& values) {
		DxUtils::CreateUploadBufferResource(device.Get(), resource, sizeof(values));
		void* mapped = nullptr;
		const D3D12_RANGE empty{};
		if (FAILED(resource->Map(0, &empty, &mapped))) return false;
		std::memcpy(mapped, &values, sizeof(values));
		resource->Unmap(0, nullptr);
		return true;
	};
	if (!upload(input, decals) || !upload(receiver, ranges) || !upload(candidates, indices)) return false;
	const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(24 * sizeof(Vector4), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	const CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)))) return false;
	DxUtils::CreateReadbackBufferResource(device.Get(), readback, buffer.Width);
	ComPtr<ID3D12CommandQueue> queue;
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> command;
	ComPtr<ID3D12Fence> fence;
	ComPtr<ID3D12DescriptorHeap> descriptors;
	const D3D12_COMMAND_QUEUE_DESC queueDesc{D3D12_COMMAND_LIST_TYPE_DIRECT};
	const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
	if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))) ||
		FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
		FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_PPV_ARGS(&command))) ||
		FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptors))) ||
		FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
	D3D12_SHADER_RESOURCE_VIEW_DESC nullTexture{};
	nullTexture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	nullTexture.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	nullTexture.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	nullTexture.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(nullptr, &nullTexture, descriptors->GetCPUDescriptorHandleForHeapStart());
	ID3D12DescriptorHeap* heaps[]{descriptors.Get()};
	command->SetDescriptorHeaps(1, heaps);
	command->SetComputeRootSignature(root.Get());
	const uint32_t counts[]{2, 3, 0, 0};
	command->SetComputeRoot32BitConstants(0, 4, counts, 0);
	command->SetComputeRootShaderResourceView(1, input->GetGPUVirtualAddress());
	command->SetComputeRootShaderResourceView(2, receiver->GetGPUVirtualAddress());
	command->SetComputeRootShaderResourceView(3, candidates->GetGPUVirtualAddress());
	command->SetComputeRootUnorderedAccessView(4, output->GetGPUVirtualAddress());
	command->SetComputeRootDescriptorTable(5, descriptors->GetGPUDescriptorHandleForHeapStart());
	const uint32_t impactCounts[4]{};
	command->SetComputeRoot32BitConstants(6, 4, impactCounts, 0);
	command->SetComputeRootShaderResourceView(7, input->GetGPUVirtualAddress());
	// GPU投影数が0でも宣言された候補Bufferを接続する
	command->SetComputeRootShaderResourceView(8, candidates->GetGPUVirtualAddress());
	command->SetComputeRootShaderResourceView(9, candidates->GetGPUVirtualAddress());
	command->Dispatch(1, 1, 1);
	const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	command->ResourceBarrier(1, &barrier);
	command->CopyResource(readback.Get(), output.Get());
	if (FAILED(command->Close())) return false;
	ID3D12CommandList* lists[]{command.Get()};
	queue->ExecuteCommandLists(1, lists);
	if (FAILED(queue->Signal(fence.Get(), 1)) || !DxDREDDiagnostics::WaitForFence(device.Get(), fence.Get(), 1, nullptr, "DecalTest")) return false;
	void* mapped = nullptr;
	const D3D12_RANGE range{0, static_cast<size_t>(buffer.Width)};
	if (FAILED(readback->Map(0, &range, &mapped))) return false;
	const auto* results = static_cast<const Vector4*>(mapped);
	bool valid = true;
	// 投影外、逆向き、Layer不一致、候補なしでは元の値を保つ
	for (uint32_t test = 0; test < 8; ++test) {
		const bool affected = test == 0 || test == 2 || test == 6;
		const Vector4 expected = test == 2 ? Vector4(0.35f, 0.6f, 0.1f, 0.6f) :
			(affected ? Vector4(0.7f, 0.2f, 0.2f, 0.6f) : Vector4(0.4f, 0.4f, 0.4f, 0.2f));
		const auto& actual = results[test * 3];
		valid &= std::abs(actual.x - expected.x) < 1e-4f && std::abs(actual.y - expected.y) < 1e-4f &&
			std::abs(actual.z - expected.z) < 1e-4f && std::abs(actual.w - expected.w) < 1e-4f;
		valid &= std::abs(results[test * 3 + 1].w - (affected ? 0.35f : 0.6f)) < 1e-4f &&
			std::abs(results[test * 3 + 2].x - (affected ? 2.0f : 0.0f)) < 1e-4f;
	}
	readback->Unmap(0, nullptr);
	return valid;
}
