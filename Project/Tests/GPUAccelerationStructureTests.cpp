#include "GPUAccelerationStructureTests.h"

//============================================================================
//	include
//============================================================================
#include "TestFixtures.h"
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/BottomLevelAccelerationStructure.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/TopLevelAccelerationStructure.h>

// c++
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
// directX
#include <d3dcompiler.h>
#include <Externals/DirectX12/d3dx12.h>

bool NEMTests::RecordASOwnerRetirement(ID3D12Device* device, ID3D12GraphicsCommandList6* commands,
	Engine::GraphicsResourceRetirement& retirement, ComPtr<ID3D12Resource>& readback) {

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 support{};
	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &support, sizeof(support))) ||
		support.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
		std::cout << "AS execution skipped: adapter has no DXR support\n";
		return true;
	}
	ComPtr<ID3D12Device8> device8;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device8)))) return false;
	const std::array<float, 9> vertices{ 0, 0, 0, 1, 0, 0, 0, 1, 0 };
	const std::array<uint32_t, 3> indices{ 0, 1, 2 };
	ComPtr<ID3D12Resource> vertexBuffer;
	ComPtr<ID3D12Resource> indexBuffer;
	DxUtils::CreateUploadBufferResource(device, vertexBuffer, sizeof(vertices));
	DxUtils::CreateUploadBufferResource(device, indexBuffer, sizeof(indices));
	void* mapped = nullptr;
	if (FAILED(vertexBuffer->Map(0, nullptr, &mapped))) return false;
	std::memcpy(mapped, vertices.data(), sizeof(vertices));
	vertexBuffer->Unmap(0, nullptr);
	if (FAILED(indexBuffer->Map(0, nullptr, &mapped))) return false;
	std::memcpy(mapped, indices.data(), sizeof(indices));
	indexBuffer->Unmap(0, nullptr);
	Engine::RaytracingBLASGeometryInput geometry{};
	geometry.vertexAddress = vertexBuffer->GetGPUVirtualAddress();
	geometry.vertexStride = sizeof(float) * 3;
	geometry.vertexCount = 3;
	geometry.indexAddress = indexBuffer->GetGPUVirtualAddress();
	geometry.indexCount = 3;
	Engine::RaytracingBLASInput input{ std::span(&geometry, 1), true };
	// Inline Ray Tracingで同frameの各構築結果を読み戻す
	const bool canQuery = support.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
	ComPtr<ID3D12RootSignature> root;
	ComPtr<ID3D12PipelineState> pipeline;
	ComPtr<ID3D12Resource> output;
	if (canQuery) {
		TestDirectory directory("ASInputLifetime");
		const auto path = directory.GetPath() / "query.hlsl";
		{
			std::ofstream file(path);
			file << R"(RaytracingAccelerationStructure scene : register(t0);
RWStructuredBuffer<uint> results : register(u0);
cbuffer Params : register(b0) { uint outputIndex; };
[numthreads(1,1,1)] void main() {
    RayDesc ray = { float3(0.25, 0.25, -1), 0, float3(0, 0, 1), 100 };
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 255, ray);
    while (query.Proceed()) {}
    results[outputIndex] = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 1 : 0;
})";
			if (!file) return false;
		}
		Engine::DxShaderCompiler compiler;
		compiler.Init();
		const auto shader = compiler.CompileShader(path.wstring(), L"cs_6_5", L"main", Engine::ShaderStage::CS);
		if (!shader.IsValid()) return false;
		CD3DX12_ROOT_PARAMETER parameters[3];
		parameters[0].InitAsShaderResourceView(0);
		parameters[1].InitAsUnorderedAccessView(0);
		parameters[2].InitAsConstants(1, 0);
		const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(3, parameters);
		ComPtr<ID3DBlob> signature, errors;
		if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors))) return false;
		if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root)))) return false;
		D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
		desc.pRootSignature = root.Get();
		desc.CS = { shader.GetBytecodePointer(), shader.GetBytecodeSize() };
		if (FAILED(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline)))) return false;
		DxUtils::CreateReadbackBufferResource(device, readback, 24);
		DxUtils::CreateUavBufferResource(device, output, 24);
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(),
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		commands->ResourceBarrier(1, &barrier);
	}
	Engine::BottomLevelAccelerationStructure source;
	source.SetRetirementQueue(retirement);
	source.Build(device8.Get(), commands, input);
	std::vector<Engine::RaytracingTLASInstance> instances(1);
	instances[0].blas = source.GetResource();
	Engine::TopLevelAccelerationStructure top;
	top.SetRetirementQueue(retirement);
	top.Build(device8.Get(), commands, instances, true);
	auto trace = [&](uint32_t index) {
		if (!canQuery) return;
		commands->SetComputeRootSignature(root.Get());
		commands->SetPipelineState(pipeline.Get());
		commands->SetComputeRootShaderResourceView(0, top.GetResource()->GetGPUVirtualAddress());
		commands->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
		commands->SetComputeRoot32BitConstant(2, index, 0);
		commands->Dispatch(1, 1, 1);
	};
	trace(0);
	// Geometryの変換を動かし、元に戻す
	geometry.localMatrix.m[3][0] = 10;
	source.Update(commands, input);
	top.Update(commands, instances);
	trace(1);
	geometry.localMatrix = Engine::Matrix4x4::Identity();
	source.Rebuild(commands, input);
	top.Rebuild(commands, instances);
	trace(2);
	// Instance側の変換も先行描画へ漏らさない
	instances[0].worldMatrix.m[3][0] = 10;
	top.Update(commands, instances);
	trace(3);
	instances[0].worldMatrix = Engine::Matrix4x4::Identity();
	top.Rebuild(commands, instances);
	trace(4);

	// 無効な再構築でも公開済みASを維持する
	auto* original = source.GetResource();
	bool rejected = false;
	try { source.Build(device8.Get(), commands, {}); }
	catch (const std::invalid_argument&) { rejected = true; }
	if (!rejected || source.GetResource() != original) return false;
	auto invalidInstances = instances;
	invalidInstances[0].blas = nullptr;
	original = top.GetResource();
	rejected = false;
	try { top.Build(device8.Get(), commands, invalidInstances, true); }
	catch (const std::invalid_argument&) { rejected = true; }
	if (!rejected || top.GetResource() != original) return false;

	Engine::BottomLevelAccelerationStructure moved(std::move(source));
	source.SetRetirementQueue(retirement);
	source.Build(device8.Get(), commands, input);
	source = std::move(moved);
	Engine::TopLevelAccelerationStructure movedTop(std::move(top));
	top.SetRetirementQueue(retirement);
	top.Build(device8.Get(), commands, instances, true);
	top = std::move(movedTop);
	trace(5);
	if (canQuery) {
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		commands->ResourceBarrier(1, &barrier);
		commands->CopyBufferRegion(readback.Get(), 0, output.Get(), 0, 24);
		retirement.Retire(std::move(output));
		retirement.Retire(std::move(root));
		retirement.Retire(std::move(pipeline));
	}
	// 頂点入力も記録済みAS構築が終わるまで保持する
	retirement.Retire(std::move(vertexBuffer));
	retirement.Retire(std::move(indexBuffer));
	std::cout << "AS build/update/rebuild and owner moves recorded\n";
	return true;
}

