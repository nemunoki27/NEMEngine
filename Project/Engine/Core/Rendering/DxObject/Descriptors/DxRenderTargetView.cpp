#include "DxRenderTargetView.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

//============================================================================
//	RTVDescriptor classMethods
//============================================================================

void Engine::RTVDescriptor::Create(uint32_t& index, D3D12_CPU_DESCRIPTOR_HANDLE& handle,
	ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC& desc) {

	index = Allocate();
	RegisterResourceName(index, resource);
	handle = GetCPUHandle(index);
	device_->CreateRenderTargetView(resource, &desc, handle);
}
