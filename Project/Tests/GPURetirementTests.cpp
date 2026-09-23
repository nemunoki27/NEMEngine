#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/BufferUploadService.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/DefaultStructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxFrameMappedUploadBuffer.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/BottomLevelAccelerationStructure.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/TopLevelAccelerationStructure.h>

// c++
#include <array>
#include <cstring>
#include <iostream>
#include <vector>
// directX
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>

#pragma comment(lib, "dxgi.lib")

namespace {

	static_assert(!std::is_copy_constructible_v<Engine::DxFrameMappedUploadBuffer>);
	static_assert(!std::is_copy_constructible_v<Engine::BottomLevelAccelerationStructure>);
	static_assert(!std::is_copy_constructible_v<Engine::TopLevelAccelerationStructure>);

	bool RecordASOwnerRetirement(ID3D12Device* device, ID3D12GraphicsCommandList6* commands,
		Engine::GraphicsResourceRetirement& retirement) {

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
		Engine::BottomLevelAccelerationStructure source;
		source.SetRetirementQueue(retirement);
		source.Build(device8.Get(), commands, input);
		Engine::BottomLevelAccelerationStructure moved(std::move(source));
		source.SetRetirementQueue(retirement);
		source.Build(device8.Get(), commands, input);
		source = std::move(moved);
		source.Update(commands, input);
		source.Rebuild(commands, input);
		std::vector<Engine::RaytracingTLASInstance> instances(1);
		instances[0].blas = source.GetResource();
		Engine::TopLevelAccelerationStructure top;
		top.SetRetirementQueue(retirement);
		top.Build(device8.Get(), commands, instances, true);
		Engine::TopLevelAccelerationStructure movedTop(std::move(top));
		top.SetRetirementQueue(retirement);
		top.Build(device8.Get(), commands, instances, true);
		top = std::move(movedTop);
		top.Update(commands, instances);
		top.Rebuild(commands, instances);
		// 頂点入力も記録済みAS構築が終わるまで保持する
		retirement.Retire(std::move(vertexBuffer));
		retirement.Retire(std::move(indexBuffer));
		std::cout << "AS build/update/rebuild and owner moves recorded\n";
		return true;
	}

	bool CheckGraphicsFenceRetirement(ID3D12Device* device, ID3D12CommandQueue* queue) {

		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList6> commands;
		ComPtr<ID3D12Fence> gate;
		ComPtr<ID3D12Fence> completed;
		if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
			FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
				IID_PPV_ARGS(&commands))) || FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))) ||
			FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&completed)))) return false;
		HANDLE completion = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!completion) return false;
		Engine::SRVDescriptor descriptors;
		descriptors.Init(device, { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE });
		Engine::GraphicsResourceRetirement retirement;
		descriptors.SetRetirementQueue(retirement);
		ComPtr<ID3D12Resource> readback;
		const std::array<uint32_t, 3> values{ 23, 29, 31 };
		DxUtils::CreateReadbackBufferResource(device, readback, sizeof(values));
		{
			Engine::StructuredInstanceBuffer<uint32_t> buffer;
			buffer.Init(device, &descriptors);
			buffer.Upload(std::span<const uint32_t>(values));
			commands->CopyBufferRegion(readback.Get(), 0, buffer.GetResource(), 0, sizeof(values));
		}
		ComPtr<ID3D12Resource> frameReadback;
		DxUtils::CreateReadbackBufferResource(device, frameReadback, sizeof(values) * 3);
		bool valid = true;
		for (uint32_t frameCount = 1; frameCount <= 3; ++frameCount) {
			Engine::GraphicsFrameState::SetActiveCount(frameCount);
			Engine::GraphicsFrameState::BeginFrame(frameCount - 1);
			Engine::DxFrameMappedUploadBuffer source;
			source.SetRetirementQueue(retirement);
			valid &= source.EnsureCapacity(device, sizeof(values), "RetirementTest");
			valid &= !source.EnsureCapacity(device, sizeof(values), "RetirementTest");
			source.Write(nullptr, 0);
			source.Write(values.data(), sizeof(values));
			commands->CopyBufferRegion(frameReadback.Get(), sizeof(values) * (frameCount - 1),
				source.GetResource(), 0, sizeof(values));
			valid &= source.EnsureCapacity(device, 512, "RetirementTest");
			Engine::DxFrameMappedUploadBuffer moved(std::move(source));
			valid &= source.GetCapacity() == 0 && source.GetResource() == nullptr;
			source.SetRetirementQueue(retirement);
			source.EnsureCapacity(device, 256, "RetirementTestMoved");
			source = std::move(moved);
			valid &= moved.GetCapacity() == 0 && moved.GetResource() == nullptr;
			source.Release();
			source.Release();
		}
		valid &= retirement.GetPendingCount() == 30;
		valid &= RecordASOwnerRetirement(device, commands.Get(), retirement);
		const size_t pendingCount = retirement.GetPendingCount();
		commands->Close();
		// 描画実行を止めたままOwnerを破棄し、完了前の回収を試す
		valid &= SUCCEEDED(queue->Wait(gate.Get(), 1));
		ID3D12CommandList* lists[]{ commands.Get() };
		queue->ExecuteCommandLists(1, lists);
		valid &= SUCCEEDED(queue->Signal(completed.Get(), 1));
		retirement.Seal(1);
		retirement.Collect(completed->GetCompletedValue());
		valid &= retirement.GetPendingCount() == pendingCount && descriptors.GetUseDescriptorCount() == 3;
		valid &= SUCCEEDED(gate->Signal(1));
		const HRESULT eventResult = completed->SetEventOnCompletion(1, completion);
		const DWORD waitResult = SUCCEEDED(eventResult) ? WaitForSingleObject(completion, 30000) : WAIT_FAILED;
		CloseHandle(completion);
		if (waitResult != WAIT_OBJECT_0) return false;
		void* mapped = nullptr;
		D3D12_RANGE readRange{ 0, sizeof(values) };
		if (FAILED(readback->Map(0, &readRange, &mapped))) return false;
		valid &= std::memcmp(mapped, values.data(), sizeof(values)) == 0;
		D3D12_RANGE writtenRange{ 0, 0 };
		readback->Unmap(0, &writtenRange);
		readRange.End = sizeof(values) * 3;
		if (FAILED(frameReadback->Map(0, &readRange, &mapped))) return false;
		for (uint32_t frameIndex = 0; frameIndex < 3; ++frameIndex) {
			valid &= std::memcmp(static_cast<std::byte*>(mapped) + sizeof(values) * frameIndex,
				values.data(), sizeof(values)) == 0;
		}
		frameReadback->Unmap(0, &writtenRange);
		retirement.Collect(completed->GetCompletedValue());
		return valid && retirement.GetPendingCount() == 0 && descriptors.GetUseDescriptorCount() == 0;
	}

	bool CheckOwnerRetirement(ID3D12Device* device, Engine::BufferUploadService& uploads) {

		Engine::SRVDescriptor descriptors;
		descriptors.Init(device, { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE });
		Engine::GraphicsResourceRetirement retirement;
		descriptors.SetRetirementQueue(retirement);
		bool valid = true;
		for (uint32_t frameCount = 1; frameCount <= 3; ++frameCount) {
			Engine::GraphicsFrameState::SetActiveCount(frameCount);
			Engine::GraphicsFrameState::BeginFrame(0);
			{
				Engine::DefaultStructuredInstanceBuffer<uint32_t> defaultBuffer;
				Engine::StructuredInstanceBuffer<uint32_t> uploadBuffer;
				defaultBuffer.Init(device, &descriptors, &uploads);
				uploadBuffer.Init(device, &descriptors);
				defaultBuffer.UploadCurrentFrame({});
				uploadBuffer.Upload(std::span<const uint32_t>{});
				for (uint32_t count : { 1u, 65u, 129u }) {
					const std::vector<uint32_t> values(count, count);
					defaultBuffer.MarkFullUpdate(count);
					defaultBuffer.UploadCurrentFrame(values);
					uploadBuffer.Upload(values);
				}
				defaultBuffer.Release();
				defaultBuffer.Release();
				uploadBuffer.Release();
				uploadBuffer.Release();
			}
			// Owner破棄後も未提出の候補はフレーム番号だけでは回収されない
			const size_t retained = retirement.GetPendingCount();
			valid &= retained == 18 && descriptors.GetUseDescriptorCount() == retained;
			Engine::GraphicsFrameState::BeginFrame(0);
			retirement.Collect(100);
			valid &= retirement.GetPendingCount() == retained;
			retirement.Seal(0);
			retirement.Collect(100);
			valid &= retirement.GetPendingCount() == retained;
			retirement.Seal(10);
			retirement.Collect(9);
			retirement.Collect(UINT64_MAX);
			valid &= descriptors.GetUseDescriptorCount() == retained;
			uploads.FlushAndWait();
			retirement.Collect(10);
			valid &= retirement.GetPendingCount() == 0 && descriptors.GetUseDescriptorCount() == 0;
		}
		// 複数提出の境界と回収後のスロット再利用を確認する
		const uint32_t first = descriptors.Allocate();
		descriptors.Retire(first, {});
		retirement.Seal(11);
		const uint32_t second = descriptors.Allocate();
		valid &= first != second;
		descriptors.Retire(second, {});
		retirement.Seal(12);
		retirement.Collect(11);
		valid &= !descriptors.IsAllocated(first) && descriptors.IsAllocated(second);
		const uint32_t reused = descriptors.Allocate();
		valid &= reused == first;
		descriptors.Free(reused);
		retirement.Collect(12);
		return valid && retirement.GetPendingCount() == 0;
	}

	bool CheckUploadOwnerLifetime(ID3D12Device* device, ID3D12CommandQueue* graphicsQueue,
		Engine::BufferUploadService& uploads) {

		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList> commandList;
		ComPtr<ID3D12Fence> fence;
		if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
			FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
				IID_PPV_ARGS(&commandList))) || FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
			return false;
		}
		commandList->Close();
		bool valid = true;
		for (uint64_t pass = 1; pass <= 2; ++pass) {
			const std::array<uint32_t, 4> values{ 13, 17, 19, static_cast<uint32_t>(pass) };
			ComPtr<ID3D12Resource> destination;
			ComPtr<ID3D12Resource> readback;
			DxUtils::CreateDefaultBufferResource(device, destination, sizeof(values));
			DxUtils::CreateReadbackBufferResource(device, readback, sizeof(values));
			ID3D12Resource* borrowed = destination.Get();
			uploads.EnqueueBufferUpload(borrowed, std::as_bytes(std::span(values)), D3D12_RESOURCE_STATE_COPY_SOURCE);
			if (pass == 1) destination.Reset();
			uploads.SubmitBatch();
			if (pass == 2) destination.Reset();
			// 転送完了の回収前に描画キューで内容を読み戻す
			allocator->Reset();
			commandList->Reset(allocator.Get(), nullptr);
			commandList->CopyBufferRegion(readback.Get(), 0, borrowed, 0, sizeof(values));
			commandList->Close();
			ID3D12CommandList* lists[]{ commandList.Get() };
			graphicsQueue->ExecuteCommandLists(1, lists);
			graphicsQueue->Signal(fence.Get(), pass);
			HANDLE completion = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (!completion) return false;
			const HRESULT eventResult = fence->SetEventOnCompletion(pass, completion);
			const DWORD waitResult = SUCCEEDED(eventResult) ? WaitForSingleObject(completion, 30000) : WAIT_FAILED;
			CloseHandle(completion);
			if (waitResult != WAIT_OBJECT_0) return false;
			void* mapped = nullptr;
			D3D12_RANGE readRange{ 0, sizeof(values) };
			if (FAILED(readback->Map(0, &readRange, &mapped))) return false;
			valid &= std::memcmp(mapped, values.data(), sizeof(values)) == 0;
			D3D12_RANGE writtenRange{ 0, 0 };
			readback->Unmap(0, &writtenRange);
			uploads.FlushAndWait();
		}
		return valid;
	}
}

bool NEMTests::TestGPURetirement(bool hardware) {

	ComPtr<ID3D12Debug1> debug;
	if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
		std::cerr << "D3D12 debug layer unavailable\n";
		return false;
	}
	debug->EnableDebugLayer();
	debug->SetEnableGPUBasedValidation(TRUE);
	ComPtr<IDXGIFactory4> factory;
	ComPtr<IDXGIAdapter> adapter;
	ComPtr<ID3D12Device> device;
	if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
	if (hardware) {
		for (UINT index = 0; ; ++index) {
			ComPtr<IDXGIAdapter1> candidate;
			if (FAILED(factory->EnumAdapters1(index, &candidate))) break;
			DXGI_ADAPTER_DESC1 description{};
			candidate->GetDesc1(&description);
			if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
			ComPtr<ID3D12Device> candidateDevice;
			if (FAILED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&candidateDevice)))) continue;
			D3D12_FEATURE_DATA_D3D12_OPTIONS5 support{};
			if (FAILED(candidateDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &support, sizeof(support))) ||
				support.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) continue;
			adapter = candidate;
			device = std::move(candidateDevice);
			break;
		}
		if (!device) {
			std::cerr << "DXR-capable hardware adapter unavailable\n";
			return false;
		}
	} else if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) ||
		FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
		std::cerr << "D3D12 WARP unavailable\n";
		return false;
	}
	DXGI_ADAPTER_DESC adapterDesc{};
	adapter->GetDesc(&adapterDesc);
	std::wcout << L"GPU adapter: " << adapterDesc.Description << L'\n';
	ComPtr<ID3D12InfoQueue> info;
	if (FAILED(device.As(&info))) return false;
	info->ClearStoredMessages();
	ComPtr<ID3D12CommandQueue> queue;
	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) return false;
	Engine::BufferUploadService uploads;
	uploads.Init(device.Get(), queue.Get());
	const uint32_t previousCount = Engine::GraphicsFrameState::GetActiveCount();
	const uint32_t previousIndex = Engine::GraphicsFrameState::GetCurrentIndex();
	bool valid = CheckGraphicsFenceRetirement(device.Get(), queue.Get());
	valid &= CheckUploadOwnerLifetime(device.Get(), queue.Get(), uploads);
	valid &= CheckOwnerRetirement(device.Get(), uploads);
	Engine::GraphicsFrameState::SetActiveCount(previousCount);
	Engine::GraphicsFrameState::SetCurrentIndex(previousIndex);
	uploads.Finalize();
	for (UINT64 index = 0; index < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index) {
		SIZE_T size = 0;
		info->GetMessage(index, nullptr, &size);
		std::vector<std::byte> storage(size);
		auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
		if (SUCCEEDED(info->GetMessage(index, message, &size)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
			std::cerr << message->pDescription << '\n';
			valid = false;
		}
	}
	return valid;
}
