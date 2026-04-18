#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Descriptors/BaseDescriptor.h>

namespace Engine {

	//============================================================================
	//	SRVDescriptor class
	//	シェーダリソース/アンオーダードアクセスのビュー(SRV/UAV)を生成・管理する。
	//============================================================================
	class SRVDescriptor :
		public BaseDescriptor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SRVDescriptor() :BaseDescriptor(2048) {};
		~SRVDescriptor() = default;

		// リソースと記述子からSRVを作成し、SRVインデックスを更新する。
		void CreateSRV(uint32_t& srvIndex, ID3D12Resource* resource,
			const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);

		// リソースと記述子からUAVを作成し、UAVインデックスを更新する。
		void CreateUAV(uint32_t& uavIndex, ID3D12Resource* resource,
			const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc);

		//--------- accessor -----------------------------------------------------

		// 最大SRV数を取得する
		uint32_t GetMaxSRVCount() const { return maxDescriptorCount_; }
	};
}; // Engine