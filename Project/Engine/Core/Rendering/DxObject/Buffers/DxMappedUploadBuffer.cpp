#include "DxMappedUploadBuffer.h"

//============================================================================
//	DxMappedUploadBuffer classMethods
//============================================================================

namespace Engine {

	void DxMappedUploadBuffer::Create(ID3D12Device* device, size_t sizeInBytes) {

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

	void DxMappedUploadBuffer::Write(const void* src, size_t sizeInBytes, size_t dstOffset) {

		// 未マップや空データは何もしない
		if (!mappedData_ || src == nullptr || sizeInBytes == 0) {
			return;
		}

		// 確保済み容量を超える転送は不具合のためアサートで弾く
		Assert::Call(dstOffset + sizeInBytes <= capacityInBytes_, "DxMappedUploadBufferの書き込みが容量を超えています");

		std::memcpy(mappedData_ + dstOffset, src, sizeInBytes);
	}
}
