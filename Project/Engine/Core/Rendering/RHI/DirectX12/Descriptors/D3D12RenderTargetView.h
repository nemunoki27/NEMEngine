#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RHI/DirectX12/Descriptors/D3D12Descriptor.h>

namespace Engine {

	//============================================================================
	//	RTVDescriptor class
	//	レンダーターゲットビュー(RTV)ディスクリプタを管理し、作成/参照を提供する。
	//============================================================================
	class RTVDescriptor :
		public BaseDescriptor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RTVDescriptor() :BaseDescriptor(128) {};
		~RTVDescriptor() = default;

		// カラーリソースからRTVを作成しCPUハンドルを割り当てる。
		void Create(uint32_t& index, D3D12_CPU_DESCRIPTOR_HANDLE& handle,
			ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC& desc);
	};
}; // Engine
