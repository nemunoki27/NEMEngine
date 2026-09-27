#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

#include <d3d12.h>

namespace Engine { class SRVDescriptor; }

namespace NEMTests {

	// Mesh差し替えの失敗復元と旧Bufferの使用を記録する
	bool RecordMeshPublication(ID3D12Device* device, ID3D12CommandQueue* queue,
		ID3D12GraphicsCommandList6* commands, Engine::SRVDescriptor& descriptors, ComPtr<ID3D12Resource>& readback);
}
