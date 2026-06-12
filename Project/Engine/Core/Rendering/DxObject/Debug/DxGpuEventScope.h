#pragma once

// directX
#include <d3d12.h>
// c++
#include <string_view>

namespace Engine {

	class DxGpuEventScope final {
	public:
		DxGpuEventScope(ID3D12GraphicsCommandList* commandList, std::string_view label);
		DxGpuEventScope(ID3D12GraphicsCommandList* commandList, const wchar_t* label);
		~DxGpuEventScope();

		DxGpuEventScope(const DxGpuEventScope&) = delete;
		DxGpuEventScope& operator=(const DxGpuEventScope&) = delete;

	private:
		ID3D12GraphicsCommandList* commandList_ = nullptr;
	};

}
