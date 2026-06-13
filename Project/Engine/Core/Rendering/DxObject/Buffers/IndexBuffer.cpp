#include "IndexBuffer.h"

using namespace Engine;

//============================================================================
//	IndexBuffer classMethods
//============================================================================
void IndexBuffer::CreateBuffer(ID3D12Device* device, UINT indexCount, DXGI_FORMAT format) {

	if (indexCount > 0) {

		// 16bit Indexを使う場合はバッファサイズも半分にする
		indexSizeInBytes_ = (format == DXGI_FORMAT_R16_UINT) ? sizeof(uint16_t) : sizeof(uint32_t);

		// インデックスデータのサイズ
		UINT sizeIB = static_cast<UINT>(indexSizeInBytes_ * indexCount);

		// IBリソースを確保しマップする
		buffer_.Create(device, sizeIB);

		indexBufferView_.BufferLocation = buffer_.GetGPUVirtualAddress();
		indexBufferView_.Format = format;
		indexBufferView_.SizeInBytes = sizeIB;
	}
}

void IndexBuffer::TransferData(const std::vector<uint32_t>& data) {

	if (indexBufferView_.Format == DXGI_FORMAT_R16_UINT) {

		// 呼び出し側が32bit配列を渡しても、IBVのFormatに合わせて16bitへ詰める
		uint16_t* dst = reinterpret_cast<uint16_t*>(buffer_.GetMappedData());
		if (dst) {
			for (size_t i = 0; i < data.size(); ++i) {
				dst[i] = static_cast<uint16_t>(data[i]);
			}
		}
	} else {

		buffer_.Write(data.data(), sizeof(uint32_t) * data.size());
	}
}

void IndexBuffer::TransferData(const std::vector<uint16_t>& data) {

	// 16bit化済みデータはそのまま転送する
	buffer_.Write(data.data(), sizeof(uint16_t) * data.size());
}
