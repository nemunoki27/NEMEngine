#include "RTVDescriptor.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/DxUtils.h>
#include <Engine/Debug/Assert.h>

//============================================================================
//	RTVDescriptor classMethods
//============================================================================

void Engine::RTVDescriptor::Create(uint32_t& index, D3D12_CPU_DESCRIPTOR_HANDLE& handle,
	ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC& desc) {

	index = Allocate();
	handle = GetCPUHandle(index);
	device_->CreateRenderTargetView(resource, &desc, handle);
}