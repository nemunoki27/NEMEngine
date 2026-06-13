#pragma once

// directX
#include <d3d12.h>
// c++
#include <string_view>

namespace Engine {

	//============================================================================
	//	DxGPUEventScope class
	// commandListにイベントを記録する
	//============================================================================
	class DxGPUEventScope final {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DxGPUEventScope(ID3D12GraphicsCommandList* commandList, std::string_view label);
		DxGPUEventScope(ID3D12GraphicsCommandList* commandList, const wchar_t* label);
		~DxGPUEventScope();

		DxGPUEventScope(const DxGPUEventScope&) = delete;
		DxGPUEventScope& operator=(const DxGPUEventScope&) = delete;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ID3D12GraphicsCommandList* commandList_ = nullptr;
	};
}