#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// directX
#include <d3d12.h>
// c++
#include <cstdint>

namespace Engine {

	constexpr uint32_t kGraphicsFrameContextCount = 3;

	//============================================================================
	//	GraphicsFrameContext structure
	//============================================================================
	struct GraphicsFrameContext {

		ComPtr<ID3D12CommandAllocator> commandAllocator{};
		uint64_t fenceValue = 0;
	};

	//============================================================================
	//	GraphicsFrameState class
	// 動的GPUリソースが参照する現在のフレームスロットを共有する
	//============================================================================
	class GraphicsFrameState {
	public:
		static void SetCurrentIndex(uint32_t index) {
			currentIndex_ = index % kGraphicsFrameContextCount;
		}
		static void BeginFrame(uint32_t index) {
			SetCurrentIndex(index);
			++frameSerial_;
		}
		static uint32_t GetCurrentIndex() { return currentIndex_; }
		static uint64_t GetFrameSerial() { return frameSerial_; }
	private:
		inline static uint32_t currentIndex_ = 0;
		inline static uint64_t frameSerial_ = 0;
	};
} // Engine
