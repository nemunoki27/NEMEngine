#include "DxShaderResourceView.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

//============================================================================
//	SRVDescriptor classMethods
//============================================================================

void SRVDescriptor::CreateSRV(uint32_t& srvIndex, ID3D12Resource* resource,
	const D3D12_SHADER_RESOURCE_VIEW_DESC& desc) {

	// SRVを作成
	srvIndex = Allocate();
	RegisterResourceName(srvIndex, resource);
	device_->CreateShaderResourceView(resource, &desc, GetCPUHandle(srvIndex));
}

void SRVDescriptor::CreateUAV(uint32_t& uavIndex, ID3D12Resource* resource,
	const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc) {

	// UAVを作成
	uavIndex = Allocate();
	RegisterResourceName(uavIndex, resource);
	device_->CreateUnorderedAccessView(resource, nullptr, &desc, GetCPUHandle(uavIndex));
}
