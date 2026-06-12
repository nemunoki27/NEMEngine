#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// directX
#include <d3d12.h>
#include <Externals/DirectX12/d3dx12.h>
// c++
#include <cstdint>

//============================================================================
//	DxUtils namespace
// D3D12のバッファ/ヒープ生成や補助関数を提供する
//============================================================================
namespace DxUtils {

	//--------- functions ----------------------------------------------------

	// ディスクリプタヒープを作成する
	void MakeDescriptorHeap(ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
		ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc);

	// バッファ用のリソース定義を作成しHeapType別の生成関数で共通利用する
	D3D12_RESOURCE_DESC MakeBufferResourceDesc(size_t sizeInBytes,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

	// CPU書き込み可能なUPLOAD heapバッファを作成する
	void CreateUploadBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes);
	// GPU専用のDEFAULT heapバッファを作成する
	// D3D12 bufferのCreateCommittedResource初期状態は実質COMMONになるため、initialStateは互換引数として残す
	void CreateDefaultBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes,
		D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

	// 通常のGPUバッファリソースをUPLOAD heapで作成し互換のためCreateUploadBufferResourceへ委譲する
	void CreateBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes);
	// UAV用途のバッファリソースを作成する
	void CreateUavBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes);
	// リードバック用のバッファリソースを作成する
	void CreateReadbackBufferResource(ID3D12Device* device, ComPtr<ID3D12Resource>& resource, size_t sizeInBytes);

	// 指定インデックスが割り当て可能かを判定する
	bool CanAllocateIndex(uint32_t useIndex, uint32_t kMaxCount);

	// 指定値をスレッド数の倍数に切り上げる
	UINT RoundUp(UINT round, UINT thread);
};
