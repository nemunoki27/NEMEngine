#include "GPUUploadReuseTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxImmutableBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/ImmutableIndexBuffer.h>
#include <Engine/Core/Rendering/DxObject/Core/DxUploadContext.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>

// c++
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {

	// 読戻しを提出して完了を確定する
	bool ExecuteReadback(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* commands) {

		ComPtr<ID3D12Fence> fence;
		if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) || FAILED(commands->Close())) return false;
		ID3D12CommandList* lists[]{commands};
		queue->ExecuteCommandLists(1, lists);
		return SUCCEEDED(queue->Signal(fence.Get(), 1)) &&
			Engine::DxDREDDiagnostics::WaitForFence(device, fence.Get(), 1, nullptr, "UploadReuseTest");
	}

	bool ReadMatches(ID3D12Resource* readback, const void* expected, size_t bytes) {

		void* mapped = nullptr;
		const D3D12_RANGE readRange{0, bytes};
		if (FAILED(readback->Map(0, &readRange, &mapped))) return false;
		const bool valid = std::memcmp(mapped, expected, bytes) == 0;
		const D3D12_RANGE writtenRange{0, 0};
		readback->Unmap(0, &writtenRange);
		return valid;
	}
}

bool NEMTests::CheckAsyncUploadLifetime(ID3D12Device* device, ID3D12CommandQueue* queue) {

	Engine::DxUploadCommand uploads;
	uploads.Create(device, queue);
	ComPtr<ID3D12Fence> gate;
	if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)))) return false;
	// 途中終了時にも待機中のQueueを解除する
	struct GateLifetime {
		ID3D12Fence* fence;
		~GateLifetime() { fence->Signal(1); }
	} lifetime{gate.Get()};
	if (FAILED(queue->Wait(gate.Get(), 1))) return false;
	ComPtr<ID3D12Resource> source;
	ComPtr<ID3D12Resource> readback;
	constexpr uint32_t expected = 0xabcdef01;
	DxUtils::CreateUploadBufferResource(device, source, sizeof(expected));
	DxUtils::CreateReadbackBufferResource(device, readback, sizeof(expected));
	void* mapped = nullptr;
	const D3D12_RANGE readRange{0, 0};
	if (FAILED(source->Map(0, &readRange, &mapped))) return false;
	std::memcpy(mapped, &expected, sizeof(expected));
	const D3D12_RANGE writtenRange{0, sizeof(expected)};
	source->Unmap(0, &writtenRange);
	{

		// 元の所有を解除しても転送コマンドに所有を残す
		const std::array<ComPtr<ID3D12Resource>, 2> resources{source, readback};
		uploads.RetainResources(resources);
		uploads.GetCommandList()->CopyBufferRegion(readback.Get(), 0, source.Get(), 0, sizeof(expected));
	}
	source.Reset();
	uploads.SubmitCommands();
	// GPUが止まっていても提出と完了確認はCPUを待機させない
	bool valid = !uploads.TryFinalize();
	bool recordingRejected = false;
	try { uploads.GetCommandList(); }
	catch (const std::logic_error&) { recordingRejected = true; }
	valid &= recordingRejected;
	if (FAILED(gate->Signal(1))) return false;
	uploads.FlushAndWait();
	valid &= uploads.TryFinalize() && ReadMatches(readback.Get(), &expected, sizeof(expected));
	// 同じAllocatorの次の転送も完了後にだけ再利用する
	uploads.ExecuteCommands({});
	return valid && uploads.TryFinalize();
}

bool NEMTests::CheckUploadPageReuse(ID3D12Device* device, ID3D12CommandQueue* queue) {

	Engine::GraphicsResourceRetirement retirement;
	Engine::BufferUploadService uploads;
	uploads.Init(retirement, device, queue);
	const uint32_t frameCount = Engine::GraphicsFrameState::GetActiveCount();
	constexpr size_t count = 192;
	std::array<uint32_t, count> expected{};
	std::array<Engine::DxImmutableBuffer, count> destinations;
	// 同じページの別範囲と、再利用後の各Batchを照合する
	for (size_t index = 0; index < count; ++index) {

		uint32_t source = static_cast<uint32_t>(index * 17 + 11);
		expected[index] = source;
		destinations[index].Create(device, uploads, std::as_bytes(std::span<const uint32_t>(&source, 1)),
			D3D12_RESOURCE_STATE_GENERIC_READ);
		source = 0;
		if (index % 16 == 15) uploads.SubmitBatch();
	}
	uploads.FlushAndWait();
	const auto pageStatistics = uploads.GetStatistics();
	bool valid = pageStatistics.uploadCount == count && pageStatistics.uploadedBytes == sizeof(expected) &&
		pageStatistics.stagingCreationCount <= 3 && pageStatistics.pooledBytes <= 3 * 1024 * 1024 &&
		Engine::GraphicsFrameState::GetActiveCount() == frameCount;
	// 大きい転送もページ内のデータを上書きしない
	std::vector<uint32_t> large(300000, 0x12345678);
	Engine::DxImmutableBuffer largeBuffer;
	largeBuffer.Create(device, uploads, std::as_bytes(std::span<const uint32_t>(large)), D3D12_RESOURCE_STATE_GENERIC_READ);
	uploads.FlushAndWait();
	valid &= uploads.GetStatistics().stagingCreationCount == pageStatistics.stagingCreationCount + 1;
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> commands;
	ComPtr<ID3D12Resource> readback;
	if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
		FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)))) return false;
	DxUtils::CreateReadbackBufferResource(device, readback, sizeof(expected) + sizeof(uint32_t));
	for (size_t index = 0; index < count; ++index) {
		commands->CopyBufferRegion(readback.Get(), index * sizeof(uint32_t), destinations[index].GetResource(), 0, sizeof(uint32_t));
	}
	commands->CopyBufferRegion(readback.Get(), sizeof(expected), largeBuffer.GetResource(), 0, sizeof(uint32_t));
	// 所有元の破棄後にも読戻しコマンドが成立する
	for (auto& destination : destinations) destination.Release();
	largeBuffer.Release();
	uploads.Finalize();
	valid &= ExecuteReadback(device, queue, commands.Get());
	std::array<uint32_t, count + 1> all{};
	std::copy(expected.begin(), expected.end(), all.begin());
	all.back() = large.front();
	valid &= ReadMatches(readback.Get(), all.data(), sizeof(all));
	retirement.Seal(1);
	retirement.Collect(1);
	return valid && retirement.GetPendingCount() == 0;
}

bool NEMTests::CheckSharedIndexLifetime(ID3D12Device* device, ID3D12CommandQueue* queue) {

	Engine::GraphicsResourceRetirement retirement;
	Engine::SRVDescriptor descriptors;
	descriptors.Init(device, {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE});
	descriptors.SetRetirementQueue(retirement);
	Engine::BufferUploadService uploads;
	uploads.Init(retirement, device, queue);
	const std::array<uint32_t, 3> values{65536, 2, 70000};
	Engine::MeshStructuredHandle<uint32_t> structured;
	structured.Create(device, uploads, descriptors, values, L"SharedIndexTest");
	Engine::ImmutableIndexBuffer indices;
	indices.Share32BitResource(retirement, structured.buffer->GetResource(), static_cast<uint32_t>(values.size()));
	bool valid = indices.GetResource() == structured.buffer->GetResource() &&
		indices.GetFormat() == DXGI_FORMAT_R32_UINT && indices.GetIndexBufferView().SizeInBytes == sizeof(values);
	// SRV所有元を先に破棄してもIBVの所有は残る
	structured.Release();
	uploads.FlushAndWait();
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> commands;
	ComPtr<ID3D12Resource> readback;
	if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
		FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)))) return false;
	DxUtils::CreateReadbackBufferResource(device, readback, sizeof(values));
	commands->CopyBufferRegion(readback.Get(), 0, indices.GetResource(), 0, sizeof(values));
	indices = Engine::ImmutableIndexBuffer{};
	uploads.Finalize();
	valid &= ExecuteReadback(device, queue, commands.Get()) && ReadMatches(readback.Get(), values.data(), sizeof(values));
	retirement.Seal(1);
	retirement.Collect(1);
	return valid && descriptors.GetUseDescriptorCount() == 0 && retirement.GetPendingCount() == 0;
}
