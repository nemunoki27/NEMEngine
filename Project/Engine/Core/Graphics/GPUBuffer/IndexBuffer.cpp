#include "IndexBuffer.h"

using namespace Engine;

//============================================================================
//	IndexBuffer classMethods
//============================================================================

void IndexBuffer::CreateBuffer(ID3D12Device* device, UINT indexCount) {

	HRESULT hr;

	if (indexCount > 0) {

		// インデックスデータのサイズ
		UINT sizeIB = static_cast<UINT>(sizeof(uint32_t) * indexCount);

		// 定数バッファーのリソース作成
		DxUtils::CreateBufferResource(device, resource_, sizeIB);

		indexBufferView_.BufferLocation = resource_->GetGPUVirtualAddress();
		indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
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

		std::memcpy(mappedData_, data.data(), sizeof(uint32_t) * data.size());
	}
}
