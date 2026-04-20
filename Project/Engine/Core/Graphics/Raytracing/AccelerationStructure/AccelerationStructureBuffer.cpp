#include "AccelerationStructureBuffer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>

//============================================================================
//	AccelerationStructureBuffer classMethods
//============================================================================

void Engine::AccelerationStructureBuffer::Create(ID3D12Device* device, UINT64 sizeInBytes,
	D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState, D3D12_HEAP_TYPE heapType) {

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
	HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&desc, initialState, nullptr, IID_PPV_ARGS(&resource_));
	assert(SUCCEEDED(hr));
}

constexpr UINT64 Engine::AccelerationStructureBuffer::AlignASSize(UINT64 value) {
	return (value + (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1)) &
		~(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1);
}