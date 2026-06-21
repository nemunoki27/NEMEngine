#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// c++
#include <cstddef>
#include <cstring>

namespace Engine {

	//============================================================================
	//	DxMappedUploadBuffer class
	// CPUからGPUへ転送するUPLOAD heapバッファの共通土台でMap保持と転送を集約する
	//============================================================================
	class DxMappedUploadBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DxMappedUploadBuffer() = default;
		~DxMappedUploadBuffer() = default;

		// 指定バイト数でUPLOAD heapリソースを確保し永続マップする
		void Create(ID3D12Device* device, size_t sizeInBytes);

		// マップ領域へ先頭からのオフセット位置にバイト列を書き込む
		void Write(const void* src, size_t sizeInBytes, size_t dstOffset = 0);

		//--------- accessor -----------------------------------------------------

		// 内部リソースを取得する
		ID3D12Resource* GetResource() const { return resource_.Get(); }
		// GPU仮想アドレスを取得する
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return resource_->GetGPUVirtualAddress(); }
		// マップ先頭ポインタを取得する
		std::byte* GetMappedData() const { return mappedData_; }
		// 確保済みバイト数を取得する
		size_t GetCapacityInBytes() const { return capacityInBytes_; }

		// リソースの作成状態を取得する
		bool IsCreatedResource() const { return isCreated_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_;
		std::byte* mappedData_ = nullptr;

		// 確保したバイト数で転送時の容量チェックに使う
		size_t capacityInBytes_ = 0;

		bool isCreated_ = false;
	};

	//============================================================================
	//	DxMappedUploadBuffer methods
	//============================================================================
	inline void DxMappedUploadBuffer::Create(ID3D12Device* device, size_t sizeInBytes) {

		// サイズ0でのリソース作成は行わない
		if (sizeInBytes == 0) {
			return;
		}

		// UPLOAD heapのバッファリソースを作成する
		DxUtils::CreateBufferResource(device, resource_, sizeInBytes);

		// マッピング
		HRESULT hr = resource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
		Assert::Call(SUCCEEDED(hr), "DxMappedUploadBufferのMapに失敗しました");

		capacityInBytes_ = sizeInBytes;
		isCreated_ = true;
	}

	inline void DxMappedUploadBuffer::Write(const void* src, size_t sizeInBytes, size_t dstOffset) {

		// 未マップや空データは何もしない
		if (!mappedData_ || src == nullptr || sizeInBytes == 0) {
			return;
		}

		// 確保済み容量を超える転送は不具合のためアサートで弾く
		Assert::Call(dstOffset + sizeInBytes <= capacityInBytes_, "DxMappedUploadBufferの書き込みが容量を超えています");

		std::memcpy(mappedData_ + dstOffset, src, sizeInBytes);
	}

} // Engine
