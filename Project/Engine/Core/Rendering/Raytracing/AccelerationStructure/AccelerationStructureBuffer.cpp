#include "AccelerationStructureBuffer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>
#include <stdexcept>

//============================================================================
//	AccelerationStructureBuffer classMethods
//============================================================================
void Engine::AccelerationStructureBuffer::Create(ID3D12Device* device, UINT64 sizeInBytes,
	D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType) {

	if (!device || sizeInBytes == 0) throw std::invalid_argument("ASBufferのDeviceまたは容量が不正です");
	const UINT64 alignedSize = AlignASSize(sizeInBytes);

	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.Type = heapType;

	// リソース記述
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = alignedSize;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = flags;

	// リソース作成
	ComPtr<ID3D12Resource> candidate;
	HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&desc, initialState, nullptr, IID_PPV_ARGS(&candidate));
	if (!DxDredDiagnostics::CheckHRESULT(device, hr, "AccelerationStructureBuffer::Create")) {
		throw std::runtime_error("AccelerationStructure用バッファの作成に失敗しました");
	}
	resource_ = std::move(candidate);
}

constexpr UINT64 Engine::AccelerationStructureBuffer::AlignASSize(UINT64 value) {
	if (value > UINT64_MAX - (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1)) {
		throw std::length_error("ASBufferの容量が大きすぎます");
	}
	return (value + (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1)) &
		~(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1);
}

void Engine::AccelerationStructureBuffer::InsertUAVBarrier(ID3D12GraphicsCommandList* commandList) const {

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.UAV.pResource = resource_.Get();
	commandList->ResourceBarrier(1, &barrier);
}
