#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/ComPtr.h>

// directX
#include <d3d12.h>
#include <Externals/DirectX12/d3dx12.h>
// c++
#include <cstdint>

//============================================================================
//	DxUtils namespace
//	D3D12のバッファ/ヒープ生成や補助関数を提供する。
//============================================================================
namespace DxUtils {

	//--------- functions ----------------------------------------------------

	// ディスクリプタヒープを作成する。
	void MakeDescriptorHeap(ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
		ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc);

	// 通常のGPUバッファリソースを作成する。
	void CreateBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes);
	// UAV用途のバッファリソースを作成する。
	void CreateUavBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes);
	// リードバック用のバッファリソースを作成する。
	void CreateReadbackBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes);

	// 指定インデックスが割り当て可能かを判定する。
	bool CanAllocateIndex(uint32_t useIndex, uint32_t kMaxCount);

	// 指定値をスレッド数の倍数に切り上げる。
	UINT RoundUp(UINT round, UINT thread);
};