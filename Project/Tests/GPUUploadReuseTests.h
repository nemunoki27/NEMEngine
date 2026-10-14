#pragma once

#include <d3d12.h>

namespace NEMTests {

	bool CheckUploadPageReuse(ID3D12Device* device, ID3D12CommandQueue* queue);
	bool CheckSharedIndexLifetime(ID3D12Device* device, ID3D12CommandQueue* queue);
	bool CheckAsyncUploadLifetime(ID3D12Device* device, ID3D12CommandQueue* queue);
}
