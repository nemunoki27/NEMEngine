#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

namespace Engine { class SRVDescriptor; }

namespace NEMTests {

	// 消失時の待機中止と保持解除を確認する
	bool CheckFenceWaitAndRemoval();


	// Pipeline所有元を破棄した後のGPU実行を記録する
	bool RecordPipelineOwnerRetirement(ID3D12Device* device, ID3D12GraphicsCommandList6* commands,
		Engine::SRVDescriptor& descriptors, ComPtr<ID3D12Resource>& readback);
	bool RecordTexturePublication(ID3D12Device* device,
		ID3D12GraphicsCommandList6* commands, Engine::SRVDescriptor& descriptors, ComPtr<ID3D12Resource>& readback);
	bool RecordImGuiRetirement(ID3D12Device* device, ID3D12CommandQueue* queue,
		ID3D12GraphicsCommandList6* commands, Engine::SRVDescriptor& descriptors);
}
