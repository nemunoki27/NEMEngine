#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

namespace NEMTests {

	// Pipeline所有元を破棄した後のGPU実行を記録する
	bool RecordPipelineOwnerRetirement(ID3D12Device* device, ID3D12GraphicsCommandList6* commands,
		Engine::GraphicsResourceRetirement& retirement, ComPtr<ID3D12Resource>& readback);
}
