#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// c++
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

// directX
#include <d3d12.h>

namespace Engine {

	// GPUリソース配列が確保する最大フレーム数
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
		static void SetActiveCount(uint32_t count);
		static void SetCurrentIndex(uint32_t index) { currentIndex_ = index % activeCount_; }
		static void BeginFrame(uint32_t index);
		static uint32_t GetCurrentIndex() { return currentIndex_; }
		static uint32_t GetActiveCount() { return activeCount_; }
		static uint64_t GetFrameSerial() { return frameSerial_; }
	private:
		inline static uint32_t currentIndex_ = 0;
		inline static uint32_t activeCount_ =
			kGraphicsFrameContextCount;
		inline static uint64_t frameSerial_ = 0;
	};

	//============================================================================
	//	GraphicsDeferredReleaseQueue class
	// GPUが参照中のリソースをフレームコンテキスト再利用まで保持する
	//============================================================================
	class GraphicsDeferredReleaseQueue {
	public:
		// 現在フレームで不要になったリソースを遅延解放する
		void Retire(ComPtr<ID3D12Resource> resource);

		// 再利用可能になったフレームスロットのリソースを解放する
		void Collect();

		// 保持中の全リソースを解放する
		void Clear();
	private:
		std::array<std::vector<ComPtr<ID3D12Resource>>,
			kGraphicsFrameContextCount> resources_{};
		std::array<uint64_t,
			kGraphicsFrameContextCount> frameSerials_{};
	};
} // Engine
