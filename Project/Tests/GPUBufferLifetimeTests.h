#pragma once

#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

namespace Engine { class SRVDescriptor; }

namespace NEMTests {

	bool CheckDescriptorCapacity(ID3D12Device* device);
	bool CheckBufferPublication(ID3D12Device* device);
	bool CheckRenderTargetPublication(ID3D12Device* device);
	bool CheckBufferCacheRetirement(ID3D12Device* device, ID3D12CommandQueue* queue);
	bool CheckDifferentialBufferUpdates(ID3D12Device* device, ID3D12CommandQueue* queue);
	bool RecordStaticBufferRetirement(ID3D12Device* device, ID3D12CommandQueue* queue,
		ID3D12GraphicsCommandList6* commands, Engine::SRVDescriptor& descriptors,
		ComPtr<ID3D12Resource>& readback);
}
