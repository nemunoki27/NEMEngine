#include "GPUBufferLifetimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxMappedUploadBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxFrameMappedUploadBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxImmutableBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/DefaultStructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxDepthStencilView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

// c++
#include <cstring>
#include <stdexcept>
#include <vector>

bool NEMTests::CheckDescriptorCapacity(ID3D12Device* device) {

	Engine::BaseDescriptor descriptors(2);
	const Engine::DescriptorType type{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
	descriptors.Init(device, type);
	bool missingRetirementRejected = false;
	try { descriptors.GetRetirementQueue(); }
	catch (const std::logic_error&) { missingRetirementRejected = true; }
	if (!missingRetirementRejected) return false;
	const uint32_t first = descriptors.Allocate();
	const uint32_t second = descriptors.Allocate();
	bool rejected = false;
	try { descriptors.Allocate(); }
	catch (const std::length_error&) { rejected = true; }
	if (!rejected || descriptors.GetUseDescriptorCount() != 2 || descriptors.GetHighWaterMark() != 2) return false;
	// 再初期化の失敗でも使用中の番号を維持する
	rejected = false;
	try { descriptors.Init(device, type); }
	catch (const std::logic_error&) { rejected = true; }
	if (!rejected || !descriptors.IsAllocated(first) || !descriptors.IsAllocated(second)) return false;
	rejected = false;
	try { descriptors.Free(UINT32_MAX); }
	catch (const std::out_of_range&) { rejected = true; }
	if (!rejected || descriptors.GetUseDescriptorCount() != 2) return false;
	descriptors.Free(first);
	if (descriptors.Allocate() != first) return false;
	descriptors.Free(first);
	descriptors.Free(second);
	return descriptors.GetUseDescriptorCount() == 0;
}

bool NEMTests::CheckBufferPublication(ID3D12Device* device) {

	Engine::GraphicsResourceRetirement retirement;
	Engine::DxMappedUploadBuffer upload;
	upload.Create(retirement, device, 8);
	auto* original = upload.GetResource();
	auto* mapped = upload.GetMappedData();
	bool rejected = false;
	// 無効な再生成で旧ResourceとMap先を失わない
	try { upload.Create(retirement, device, 0); }
	catch (const std::invalid_argument&) { rejected = true; }
	if (!rejected || upload.GetResource() != original || upload.GetMappedData() != mapped) return false;
	const uint32_t value = 37;
	upload.Write(&value, sizeof(value));
	rejected = false;
	try { upload.Write(&value, sizeof(value), SIZE_MAX); }
	catch (const std::out_of_range&) { rejected = true; }
	if (!rejected || std::memcmp(mapped, &value, sizeof(value)) != 0) return false;

	Engine::DxStructuredBuffer<uint32_t> structured;
	structured.CreateSRVBuffer(device, 2);
	original = structured.GetResource();
	rejected = false;
	try { structured.CreateSRVBuffer(device, 0); }
	catch (const std::invalid_argument&) { rejected = true; }
	if (!rejected || structured.GetResource() != original) return false;
	structured.TransferData(&value, 1);
	// UAVへの変更後は解放済みのMap先へ触れない
	structured.CreateUAVBuffer(device, 2);
	structured.TransferData(&value, 1);

	Engine::DxFrameMappedUploadBuffer frames;
	frames.SetRetirementQueue(retirement);
	if (!frames.EnsureCapacity(device, 8, "BufferPublication", 0)) return false;
	original = frames.GetResource();
	rejected = false;
	try { frames.EnsureCapacity(nullptr, 16, "BufferPublication", 0); }
	catch (const std::invalid_argument&) { rejected = true; }
	if (!rejected || frames.GetResource() != original || frames.GetCapacity() != 8) return false;
	frames.Write(&value, sizeof(value));
	rejected = false;
	try { frames.Write(&value, sizeof(value), SIZE_MAX); }
	catch (const std::out_of_range&) { rejected = true; }
	frames.Release();
	if (!rejected) return false;

	// AS用の大きな入力とCBVの上限を区別する
	Engine::FrameUploadBufferAllocator general;
	Engine::FrameConstantBufferAllocator constants;
	const std::vector<uint8_t> largeInput(65537, 1);
	if (!general.AllocateAndUploadBytes(retirement, device, largeInput).gpuAddress) return false;
	rejected = false;
	try { constants.AllocateAndUploadBytes(retirement, device, largeInput); }
	catch (const std::length_error&) { rejected = true; }
	return rejected;
}

bool NEMTests::CheckRenderTargetPublication(ID3D12Device* device) {

	Engine::RTVDescriptor targets;
	Engine::DSVDescriptor depths;
	Engine::SRVDescriptor shaders;
	Engine::GraphicsResourceRetirement retirement;
	targets.Init(device, { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE });
	depths.Init(device, { D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE });
	shaders.Init(device, { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE });
	targets.SetRetirementQueue(retirement);
	depths.SetRetirementQueue(retirement);
	shaders.SetRetirementQueue(retirement);
	Engine::MultiRenderTarget surface;
	Engine::MultiRenderTargetCreateDesc desc{};
	desc.width = 4;
	desc.height = 4;
	desc.colors.emplace_back();
	desc.depth = Engine::DepthTextureCreateDesc{ .width = 4, .height = 4 };
	surface.Create(device, &targets, &depths, &shaders, desc);
	auto* originalColor = surface.GetColorTexture(0)->GetResource();
	auto* originalDepth = surface.GetDepthTexture()->GetResource();
	std::vector<uint32_t> occupied;
	for (;;) {
		try { occupied.push_back(depths.Allocate()); }
		catch (const std::length_error&) { break; }
	}
	// 色Textureの作成後に深度Descriptorを不足させる
	desc.width = 8;
	bool rejected = false;
	try { surface.Create(device, &targets, &depths, &shaders, desc); }
	catch (const std::length_error&) { rejected = true; }
	const bool preserved = rejected && surface.GetWidth() == 4 &&
		surface.GetColorTexture(0)->GetResource() == originalColor &&
		surface.GetDepthTexture()->GetResource() == originalDepth;
	for (uint32_t index : occupied) depths.Free(index);
	surface.Destroy();
	// 描画を提出していないfixtureの保持を回収する
	retirement.Seal(1);
	retirement.Collect(1);
	return preserved && targets.GetUseDescriptorCount() == 0 && depths.GetUseDescriptorCount() == 0 &&
		shaders.GetUseDescriptorCount() == 0 && retirement.GetPendingCount() == 0;
}

bool NEMTests::RecordStaticBufferRetirement(ID3D12Device* device, ID3D12CommandQueue* queue,
	ID3D12GraphicsCommandList6* commands, Engine::SRVDescriptor& descriptors,
	ComPtr<ID3D12Resource>& readback) {

	auto& retirement = descriptors.GetRetirementQueue();
	Engine::BufferUploadService uploads;
	uploads.Init(retirement, device, queue);
	DxUtils::CreateReadbackBufferResource(device, readback, 24);
	Engine::DxImmutableBuffer buffer;
	const uint32_t values[]{ 101, 103 };
	// 再生成前後の静的Bufferを別々に読み戻す
	for (uint32_t index = 0; index < 2; ++index) {
		buffer.Create(device, uploads, std::as_bytes(std::span<const uint32_t>(&values[index], 1)),
			D3D12_RESOURCE_STATE_GENERIC_READ);
		commands->CopyBufferRegion(readback.Get(), index * 4, buffer.GetResource(), 0, 4);
	}
	Engine::DefaultStructuredInstanceBuffer<uint32_t> changes;
	changes.Init(device, &descriptors, &uploads);
	const uint32_t changed[]{ 107, 109 };
	for (uint32_t index = 0; index < 2; ++index) {
		changes.MarkFullUpdate(1);
		changes.UploadCurrentFrame(std::span<const uint32_t>(&changed[index], 1));
		commands->CopyBufferRegion(readback.Get(), 8 + index * 4, changes.GetResource(), 0, 4);
	}
	changes.Release();

	// 移動と再生成後も先行描画のBufferとSRVを保持する
	Engine::MeshStructuredHandle<uint32_t> mesh;
	const uint32_t meshValues[]{ 113, 127 };
	for (uint32_t index = 0; index < 2; ++index) {
		mesh.Create(device, uploads, descriptors, std::span<const uint32_t>(&meshValues[index], 1), L"MeshLifetimeTest");
		commands->CopyBufferRegion(readback.Get(), 16 + index * 4, mesh.buffer->GetResource(), 0, 4);
	}
	Engine::MeshStructuredHandle<uint32_t> moved(std::move(mesh));
	if (mesh.buffer || mesh.srvIndex != UINT32_MAX) return false;
	moved.Release();
	// Serviceの破棄が未提出の転送も完了させる
	buffer.Release();
	return true;
}

bool NEMTests::CheckDifferentialBufferUpdates(ID3D12Device* device, ID3D12CommandQueue* queue) {

	Engine::SRVDescriptor descriptors;
	Engine::GraphicsResourceRetirement retirement;
	descriptors.Init(device, { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE });
	descriptors.SetRetirementQueue(retirement);
	Engine::BufferUploadService uploads;
	uploads.Init(retirement, device, queue);
	Engine::DefaultStructuredInstanceBuffer<uint32_t> buffer;
	buffer.Init(device, &descriptors, &uploads);
	ComPtr<ID3D12CommandAllocator> allocator;
	ComPtr<ID3D12GraphicsCommandList> commands;
	ComPtr<ID3D12Fence> fence;
	ComPtr<ID3D12Resource> readback;
	if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
		FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands))) ||
		FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
	DxUtils::CreateReadbackBufferResource(device, readback, 12);
	uint32_t values[]{ 11, 13, 17 };
	const size_t expectedBytes[]{ 12, 4, 0 };
	bool valid = true;
	for (uint32_t phase = 0; phase < 3; ++phase) {
		Engine::GraphicsFrameState::BeginFrame(0);
		if (phase == 0) buffer.MarkFullUpdate(3);
		if (phase == 1) { values[1] = 19; buffer.MarkDirtyRange(1, 1); }
		valid &= buffer.UploadCurrentFrame(values) == expectedBytes[phase];
		uploads.FlushAndWait();
		commands->CopyBufferRegion(readback.Get(), 0, buffer.GetResource(), 0, 12);
		if (FAILED(commands->Close())) return false;
		ID3D12CommandList* lists[]{ commands.Get() };
		queue->ExecuteCommandLists(1, lists);
		if (FAILED(queue->Signal(fence.Get(), phase + 1))) return false;
		// GPU完了後に同じframe枠を再利用する
		if (!Engine::DxDredDiagnostics::WaitForFence(device, fence.Get(), phase + 1, nullptr, "DifferentialBufferTest")) return false;
		void* mapped = nullptr;
		D3D12_RANGE range{ 0, 12 };
		if (FAILED(readback->Map(0, &range, &mapped))) return false;
		valid &= std::memcmp(mapped, values, sizeof(values)) == 0;
		D3D12_RANGE written{ 0, 0 };
		readback->Unmap(0, &written);
		if (phase < 2 && (FAILED(allocator->Reset()) || FAILED(commands->Reset(allocator.Get(), nullptr)))) return false;
	}
	buffer.Release();
	uploads.Finalize();
	retirement.Seal(3);
	retirement.Collect(3);
	return valid && descriptors.GetUseDescriptorCount() == 0 && retirement.GetPendingCount() == 0;
}

bool NEMTests::CheckBufferCacheRetirement(ID3D12Device* device, ID3D12CommandQueue* queue) {

	Engine::GraphicsResourceRetirement retirement;
	Engine::SRVDescriptor descriptors;
	descriptors.Init(device, { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE });
	descriptors.SetRetirementQueue(retirement);
	Engine::StructuredInstanceBuffer<uint32_t> buffer;
	buffer.Init(device, &descriptors);
	Engine::FrameConstantBufferAllocator constants(256);
	Engine::GraphicsFrameState::BeginFrame(0);
	const std::vector<uint32_t> large(2048, 131);
	for (uint32_t index = 0; index < 3; ++index) buffer.Upload(large);
	const std::vector<uint8_t> largeConstants(65536, 0);
	constants.AllocateAndUploadBytes(retirement, device, largeConstants);
	const uint32_t value = 137;
	const auto previous = constants.AllocateAndUpload(retirement, device, value);
	const uint32_t originalDescriptors = descriptors.GetUseDescriptorCount();
	Engine::FrameConstantBufferAllocation current{};

	// 少量の利用が続いた後に余剰slotと過大容量を回収する
	for (uint64_t frame = 0; frame < Engine::kGraphicsResourceReuseFrames + 3; ++frame) {
		Engine::GraphicsFrameState::BeginFrame(0);
		buffer.Upload(std::span<const uint32_t>(&value, 1));
		current = constants.AllocateAndUpload(retirement, device, value);
	}
	bool valid = buffer.GetResource()->GetDesc().Width == 64 * sizeof(uint32_t) &&
		current.gpuAddress != previous.gpuAddress && descriptors.GetUseDescriptorCount() > originalDescriptors;

	// 回収候補はFenceを完了させるまで番号を返さない
	ComPtr<ID3D12Fence> fence;
	if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
	retirement.Seal(1);
	retirement.Collect(0);
	valid &= descriptors.GetUseDescriptorCount() > originalDescriptors;
	if (FAILED(queue->Signal(fence.Get(), 1)) ||
		!Engine::DxDredDiagnostics::WaitForFence(device, fence.Get(), 1, nullptr, "BufferCacheTest")) return false;
	retirement.Collect(1);
	valid &= descriptors.GetUseDescriptorCount() == 1;
	buffer.Release();
	constants.Release();
	retirement.Seal(1);
	retirement.Collect(1);
	return valid && descriptors.GetUseDescriptorCount() == 0 && retirement.GetPendingCount() == 0;
}
