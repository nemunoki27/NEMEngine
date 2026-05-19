#include "IndexBuffer.h"

using namespace Engine;

//============================================================================
//	IndexBuffer classMethods
//============================================================================

void IndexBuffer::CreateBuffer(ID3D12Device* device, UINT indexCount, DXGI_FORMAT format) {

	HRESULT hr;

	if (indexCount > 0) {

		// 16bit Indexを使う場合はバッファサイズも半分にする
		indexSizeInBytes_ = (format == DXGI_FORMAT_R16_UINT) ? sizeof(uint16_t) : sizeof(uint32_t);

		// インデックスデータのサイズ
		UINT sizeIB = static_cast<UINT>(indexSizeInBytes_ * indexCount);

		// 定数バッファーのリソース作成
		DxUtils::CreateBufferResource(device, resource_, sizeIB);

		indexBufferView_.BufferLocation = resource_->GetGPUVirtualAddress();
		indexBufferView_.Format = format;
		indexBufferView_.SizeInBytes = sizeIB;

		// マッピング
		hr = resource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
		assert(SUCCEEDED(hr));

		// 作成済みにする
		isCreated_ = true;
	}
}

void IndexBuffer::TransferData(const std::vector<uint32_t>& data) {

	if (mappedData_) {

		if (indexBufferView_.Format == DXGI_FORMAT_R16_UINT) {

			// 呼び出し側が32bit配列を渡しても、IBVのFormatに合わせて16bitへ詰める
			uint16_t* dst = reinterpret_cast<uint16_t*>(mappedData_);
			for (size_t i = 0; i < data.size(); ++i) {
				dst[i] = static_cast<uint16_t>(data[i]);
			}
		} else {

			std::memcpy(mappedData_, data.data(), sizeof(uint32_t) * data.size());
		}
	}
}

void IndexBuffer::TransferData(const std::vector<uint16_t>& data) {

	if (mappedData_) {

		// 16bit化済みデータはそのまま転送する
		std::memcpy(mappedData_, data.data(), sizeof(uint16_t) * data.size());
	}
}
