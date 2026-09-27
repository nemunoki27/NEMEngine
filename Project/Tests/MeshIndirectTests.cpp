#include "GPUPipelineRetirementTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchResources.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Externals/DirectX12/d3dx12.h>
#include <array>
#include <cstring>
#include <iostream>

bool NEMTests::CheckMeshIndirectArguments(ID3D12Device* device, ID3D12CommandQueue* queue) {

	using namespace Engine;
	constexpr uint32_t count = 513;
	constexpr uint32_t argsBytes = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) * kMeshLODCount;
	constexpr uint32_t instanceBytes = sizeof(MeshInstanceData) * count;
	constexpr uint32_t resultBytes = argsBytes + instanceBytes;
	DxShaderCompiler compiler;
	compiler.Init();
	const auto path = RuntimePaths::GetEngineAssetsRoot() / "Shaders/Builtin/Mesh/Culling/buildIndexedIndirectArgs.CS.hlsl";
	const auto shader = compiler.CompileShader(path.wstring(), L"cs_6_0", L"main", ShaderStage::CS);
	if (!shader.IsValid()) return false;

	// 製品と同じComputeへ既知のInstance配列を渡す
	CD3DX12_DESCRIPTOR_RANGE depthRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
	CD3DX12_ROOT_PARAMETER parameters[7];
	parameters[0].InitAsConstantBufferView(1);
	parameters[1].InitAsConstantBufferView(2);
	parameters[2].InitAsShaderResourceView(0);
	parameters[3].InitAsShaderResourceView(3, 1);
	parameters[4].InitAsUnorderedAccessView(0);
	parameters[5].InitAsUnorderedAccessView(1);
	parameters[6].InitAsDescriptorTable(1, &depthRange);
	const CD3DX12_ROOT_SIGNATURE_DESC rootDesc(7, parameters);
	ComPtr<ID3DBlob> signature, errors;
	ComPtr<ID3D12RootSignature> root;
	ComPtr<ID3D12PipelineState> pipeline;
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)) ||
		FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root)))) return false;
	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = root.Get();
	pipelineDesc.CS = { shader.GetBytecodePointer(), shader.GetBytecodeSize() };
	if (FAILED(device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)))) return false;

	ComPtr<ID3D12DescriptorHeap> heap;
	const D3D12_DESCRIPTOR_HEAP_DESC heapDesc{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
	if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)))) return false;
	D3D12_SHADER_RESOURCE_VIEW_DESC nullDepth{};
	nullDepth.Format = DXGI_FORMAT_R32_FLOAT;
	nullDepth.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	nullDepth.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	nullDepth.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(nullptr, &nullDepth, heap->GetCPUDescriptorHandleForHeapStart());

	ComPtr<ID3D12Resource> input, draws, subMeshes, visible, args, readback;
	DxUtils::CreateUploadBufferResource(device, input, instanceBytes);
	DxUtils::CreateUploadBufferResource(device, draws, 256);
	DxUtils::CreateUploadBufferResource(device, subMeshes, sizeof(MeshSubMeshShaderData));
	DxUtils::CreateUavBufferResource(device, visible, instanceBytes * kMeshLODCount);
	DxUtils::CreateUavBufferResource(device, args, argsBytes);
	DxUtils::CreateReadbackBufferResource(device, readback, resultBytes * 2);
	std::array<MeshInstanceData, count> instances{};
	for (uint32_t index = 0; index < count; ++index) {
		instances[index].worldMatrix.m[3][2] = 10.0f;
		instances[index].entityIndex = index;
		instances[index].subMeshDataOffset = index * 3;
	}
	auto upload = [](ID3D12Resource* resource, const void* data, size_t size) {
		void* mapped = nullptr;
		const D3D12_RANGE empty{ 0, 0 };
		if (FAILED(resource->Map(0, &empty, &mapped))) return false;
		std::memcpy(mapped, data, size);
		resource->Unmap(0, nullptr);
		return true;
	};
	MeshDrawConstants draw{};
	draw.instanceCount = count;
	draw.meshBoundsRadius = 1.0f;
	draw.lodPixelThresholds = Vector3(100.0f, 40.0f, 10.0f);
	draw.lodIndexOffsets = { 0, 300, 420, 480 };
	draw.lodIndexCounts = { 300, 120, 60, 30 };
	GraphicsResourceRetirement retirement;
	MeshBatchViewResources views;
	views.Init(retirement, device);
	std::array<D3D12_GPU_VIRTUAL_ADDRESS, 2> viewAddresses{};
	ResolvedRenderView cullingView{};
	cullingView.valid = cullingView.perspective.valid = true;
	for (uint32_t viewIndex = 0; viewIndex < 2; ++viewIndex) {
		ResolvedRenderView view{};
		view.width = view.height = 1000;
		view.valid = view.perspective.valid = true;
		// カリングCameraを固定したまま描画Cameraだけ離す
		view.perspective.matrices.viewMatrix.m[3][2] = viewIndex == 0 ? 0.0f : 1000.0f;
		views.UpdateView(view, &cullingView);
		viewAddresses[viewIndex] = views.GetViewGPUAddress(view.kind);
	}
	if (!upload(input.Get(), instances.data(), sizeof(instances)) || !upload(draws.Get(), &draw, sizeof(draw)) ||
		viewAddresses[0] == viewAddresses[1]) return false;
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> commands;
	ComPtr<ID3D12Fence> fence;
	if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
		FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(), IID_PPV_ARGS(&commands))) ||
		FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
	commands->SetComputeRootSignature(root.Get());
	ID3D12DescriptorHeap* heaps[]{ heap.Get() };
	commands->SetDescriptorHeaps(1, heaps);
	commands->SetComputeRootConstantBufferView(1, draws->GetGPUVirtualAddress());
	commands->SetComputeRootShaderResourceView(2, input->GetGPUVirtualAddress());
	commands->SetComputeRootShaderResourceView(3, subMeshes->GetGPUVirtualAddress());
	commands->SetComputeRootUnorderedAccessView(4, visible->GetGPUVirtualAddress());
	commands->SetComputeRootUnorderedAccessView(5, args->GetGPUVirtualAddress());
	commands->SetComputeRootDescriptorTable(6, heap->GetGPUDescriptorHandleForHeapStart());
	for (uint32_t viewIndex = 0; viewIndex < 2; ++viewIndex) {
		const auto before = viewIndex == 0 ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_COPY_SOURCE;
		const D3D12_RESOURCE_BARRIER toWrite[]{
			CD3DX12_RESOURCE_BARRIER::Transition(args.Get(), before, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(visible.Get(), before, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) };
		commands->ResourceBarrier(2, toWrite);
		commands->SetComputeRootConstantBufferView(0, viewAddresses[viewIndex]);
		commands->Dispatch(1, 1, 1);
		const D3D12_RESOURCE_BARRIER toRead[]{
			CD3DX12_RESOURCE_BARRIER::Transition(args.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(visible.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE) };
		commands->ResourceBarrier(2, toRead);
		commands->CopyBufferRegion(readback.Get(), resultBytes * viewIndex, args.Get(), 0, argsBytes);
		const uint32_t lod = viewIndex == 0 ? 1 : 3;
		commands->CopyBufferRegion(readback.Get(), resultBytes * viewIndex + argsBytes, visible.Get(), instanceBytes * lod, instanceBytes);
	}
	if (FAILED(commands->Close())) return false;
	ID3D12CommandList* lists[]{ commands.Get() };
	queue->ExecuteCommandLists(1, lists);
	if (FAILED(queue->Signal(fence.Get(), 1))) return false;
	HANDLE completed = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!completed) return false;
	const HRESULT wait = fence->SetEventOnCompletion(1, completed);
	const DWORD result = SUCCEEDED(wait) ? WaitForSingleObject(completed, 30000) : WAIT_FAILED;
	CloseHandle(completed);
	if (result != WAIT_OBJECT_0) return false;

	// 数とLODとInstanceの内容を順序に依存せず確認する
	void* mapped = nullptr;
	const D3D12_RANGE range{ 0, resultBytes * 2 };
	if (FAILED(readback->Map(0, &range, &mapped))) return false;
	bool valid = true;
	for (uint32_t viewIndex = 0; viewIndex < 2; ++viewIndex) {
		const auto* bytes = static_cast<const std::byte*>(mapped) + resultBytes * viewIndex;
		const auto* output = reinterpret_cast<const D3D12_DRAW_INDEXED_ARGUMENTS*>(bytes);
		const uint32_t selectedLOD = viewIndex == 0 ? 1 : 3;
		for (uint32_t lod = 0; lod < kMeshLODCount; ++lod) {
			valid &= output[lod].InstanceCount == (lod == selectedLOD ? count : 0) &&
				output[lod].IndexCountPerInstance == draw.lodIndexCounts[lod] &&
				output[lod].StartIndexLocation == draw.lodIndexOffsets[lod];
		}
		std::array<bool, count> seen{};
		const auto* copied = reinterpret_cast<const MeshInstanceData*>(bytes + argsBytes);
		for (uint32_t index = 0; index < count; ++index) {
			const uint32_t id = copied[index].entityIndex;
			if (id >= count || seen[id]) { valid = false; continue; }
			seen[id] = true;
			valid &= std::memcmp(&copied[index], &instances[id], sizeof(MeshInstanceData)) == 0;
		}
	}
	const D3D12_RANGE empty{ 0, 0 };
	readback->Unmap(0, &empty);
	if (!valid) std::cerr << "Mesh indirect counts, LOD or instance data mismatch\n";
	return valid;
}
