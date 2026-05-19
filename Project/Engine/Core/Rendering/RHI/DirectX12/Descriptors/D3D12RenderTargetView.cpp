#include "D3D12RenderTargetView.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RHI/DirectX12/Common/D3D12Utils.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

//============================================================================
//	RTVDescriptor classMethods
//============================================================================

void Engine::RTVDescriptor::Create(uint32_t& index, D3D12_CPU_DESCRIPTOR_HANDLE& handle,
	ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC& desc) {

	index = Allocate();
	handle = GetCPUHandle(index);
	device_->CreateRenderTargetView(resource, &desc, handle);
}