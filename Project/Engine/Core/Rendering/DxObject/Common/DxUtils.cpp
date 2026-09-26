#include "DxUtils.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

// c++
#include <stdexcept>
#include <utility>

namespace {

	void CreateBuffer(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes,
		D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags) {

		if (!device || sizeInBytes == 0) {
			throw std::invalid_argument("Buffer作成にはDeviceと1Byte以上のサイズが必要です");
		}
		const CD3DX12_HEAP_PROPERTIES heap(heapType);
		const auto desc = DxUtils::MakeBufferResourceDesc(sizeInBytes, flags);
		ComPtr<ID3D12Resource> candidate;
		const HRESULT result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			state, nullptr, IID_PPV_ARGS(&candidate));
		if (!Engine::DxDredDiagnostics::CheckHRESULT(device, result, "DxUtils::CreateBuffer")) {
			throw std::runtime_error("GPU Bufferの作成に失敗しました");
		}
		// 失敗時は呼び出し元の資源を維持する
		resource = std::move(candidate);
	}
}

//============================================================================
//	DxUtils namespaceMethods
//============================================================================
void DxUtils::MakeDescriptorHeap(ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
	ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc) {

	if (!device || desc.NumDescriptors == 0) {
		throw std::invalid_argument("DescriptorHeapのDeviceまたは容量が不正です");
	}
	ComPtr<ID3D12DescriptorHeap> candidate;
	const HRESULT result = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&candidate));
	if (!Engine::DxDredDiagnostics::CheckHRESULT(device, result, "DxUtils::MakeDescriptorHeap")) {
		throw std::runtime_error("DescriptorHeapの作成に失敗しました");
	}
	descriptorHeap = std::move(candidate);
}

D3D12_RESOURCE_DESC DxUtils::MakeBufferResourceDesc(size_t sizeInBytes, D3D12_RESOURCE_FLAGS flags) {

	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = sizeInBytes;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = flags;
	return desc;
}

void DxUtils::CreateUploadBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes) {

	// CPUから書き込む領域を確保する
	CreateBuffer(device, resource, sizeInBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
}

void DxUtils::CreateDefaultBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes,
	[[maybe_unused]] D3D12_RESOURCE_STATES initialState, D3D12_RESOURCE_FLAGS flags) {

	// DEFAULT Bufferの状態遷移は呼び出し元で記録する
	CreateBuffer(device, resource, sizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, flags);
}

void DxUtils::CreateBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes) {

	CreateUploadBufferResource(device, resource, sizeInBytes);
}

void DxUtils::CreateUavBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes) {

	CreateDefaultBufferResource(device, resource, sizeInBytes, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
}

void DxUtils::CreateReadbackBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes) {

	// GPUから読み戻す領域を確保する
	CreateBuffer(device, resource, sizeInBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
}

bool DxUtils::CanAllocateIndex(uint32_t useIndex, uint32_t maxCount) {

	return useIndex < maxCount;
}

UINT DxUtils::RoundUp(UINT value, UINT divisor) {

	if (divisor == 0) throw std::invalid_argument("切り上げ除算の除数に0は指定できません");
	// 加算による桁あふれを避ける
	return value / divisor + (value % divisor != 0);
}
