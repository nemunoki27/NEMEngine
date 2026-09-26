#include "DxRenderTargetView.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <stdexcept>

//============================================================================
//	RTVDescriptor classMethods
//============================================================================
void Engine::RTVDescriptor::Create(uint32_t& index, D3D12_CPU_DESCRIPTOR_HANDLE& handle,
	ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC& desc) {

	const uint32_t allocated = Allocate();
	try { RegisterResourceName(allocated, resource); }
	catch (...) { Free(allocated); throw; }
	index = allocated;
	handle = GetCPUHandle(index);
	device_->CreateRenderTargetView(resource, &desc, handle);
}

void Engine::RTVDescriptor::Recreate(uint32_t index, const D3D12_CPU_DESCRIPTOR_HANDLE& handle,
	ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC& desc) {

	if (!IsAllocated(index)) throw std::out_of_range("RTV Descriptorが確保されていません");
	UpdateResourceName(index, resource);
	device_->CreateRenderTargetView(resource, &desc, handle);
}
