#pragma once

//============================================================================
//	include
//============================================================================
#include <d3d12.h>

namespace NEMTests {

	// FontとAtlasの世代、失敗時保持とDescriptor回収を検証する
	bool CheckFontRenderPublication(ID3D12Device* device, ID3D12CommandQueue* queue);
} // namespace NEMTests
