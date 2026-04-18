#include "SRVDescriptor.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/DxUtils.h>
#include <Engine/Debug/Assert.h>

//============================================================================
//	SRVDescriptor classMethods
//============================================================================

void SRVDescriptor::CreateSRV(uint32_t& srvIndex, ID3D12Resource* resource,
	const D3D12_SHADER_RESOURCE_VIEW_DESC& desc) {

	// SRVを作成
	srvIndex = Allocate();
	device_->CreateShaderResourceView(resource, &desc, GetCPUHandle(srvIndex));
}

void SRVDescriptor::CreateUAV(uint32_t& uavIndex, ID3D12Resource* resource,
	const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc) {

	// UAVを作成
	uavIndex = Allocate();
	device_->CreateUnorderedAccessView(resource, nullptr, &desc, GetCPUHandle(uavIndex));
}