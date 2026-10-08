#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

namespace NEMTests {

	// 同frameのAS更新と所有元破棄後の交差結果を記録する
	bool RecordASOwnerRetirement(ID3D12Device* device, ID3D12GraphicsCommandList6* commands,
		Engine::GraphicsResourceRetirement& retirement, ComPtr<ID3D12Resource>& readback);
}
