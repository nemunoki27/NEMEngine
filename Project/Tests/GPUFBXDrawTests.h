#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>

namespace NEMTests {

	// FBXの描画用Bufferを使って色を描画し読み戻す
	bool RecordFBXDraw(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList6* commands,
		Engine::SRVDescriptor& descriptors, ComPtr<ID3D12Resource>& readback);
	// FBXの描画結果に有効な画素があることを確認する
	bool CheckFBXDraw(ID3D12Resource* readback);
}
