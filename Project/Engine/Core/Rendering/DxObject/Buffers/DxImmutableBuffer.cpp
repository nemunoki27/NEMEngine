#include "DxImmutableBuffer.h"

//============================================================================
//	DxImmutableBuffer classMethods
//============================================================================

namespace Engine {

	void DxImmutableBuffer::Create(ID3D12Device* device, BufferUploadService& uploadService,
		std::span<const std::byte> data, D3D12_RESOURCE_STATES finalState) {

		// 空配列はバッファを作らない(Buffer Widthが0にならないようにする)
		if (data.empty()) {
			return;
		}

		// DEFAULT heapのバッファは作成時COMMONでコピー用のCOPY_DEST遷移はBufferUploadServiceで積む
		DxUtils::CreateDefaultBufferResource(device, resource_, data.size());

		uploadService.EnqueueBufferUpload(resource_.Get(), data, finalState);
	}
}
