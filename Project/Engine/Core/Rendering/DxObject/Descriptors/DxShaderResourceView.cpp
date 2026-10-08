#include "DxShaderResourceView.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <stdexcept>

//============================================================================
//	SRVDescriptor classMethods
//============================================================================
void SRVDescriptor::CreateSRV(uint32_t& srvIndex, ID3D12Resource* resource,
	const D3D12_SHADER_RESOURCE_VIEW_DESC& desc) {

	// SRVを作成
	const uint32_t index = Allocate();
	try {
		RegisterResourceName(index, resource);
	} catch (...) {
		// 未公開の番号を戻す
		Free(index);
		throw;
	}
	srvIndex = index;
	device_->CreateShaderResourceView(resource, &desc, GetCPUHandle(srvIndex));
}

void SRVDescriptor::RecreateSRV(uint32_t srvIndex, ID3D12Resource* resource,
	const D3D12_SHADER_RESOURCE_VIEW_DESC& desc) {

	if (!IsAllocated(srvIndex)) throw std::out_of_range("SRV Descriptorが確保されていません");
	// GPU完了を確認済みの既存番号へ書き込む
	RegisterResourceName(srvIndex, resource);
	device_->CreateShaderResourceView(resource, &desc, GetCPUHandle(srvIndex));
}

void SRVDescriptor::CreateUAV(uint32_t& uavIndex, ID3D12Resource* resource,
	const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc) {

	// UAVを作成
	const uint32_t index = Allocate();
	try {
		RegisterResourceName(index, resource);
	} catch (...) {
		// 未公開の番号を戻す
		Free(index);
		throw;
	}
	uavIndex = index;
	device_->CreateUnorderedAccessView(resource, nullptr, &desc, GetCPUHandle(uavIndex));
}
