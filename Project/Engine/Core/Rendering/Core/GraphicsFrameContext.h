#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// c++
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

// directX
#include <d3d12.h>

namespace Engine {

	class BaseDescriptor;

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
	//	GraphicsResourceRetirement class
	//	描画提出後のFence完了まで資源とDescriptorを保持する
	//============================================================================
	class GraphicsResourceRetirement {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		GraphicsResourceRetirement() = default;
		GraphicsResourceRetirement(const GraphicsResourceRetirement&) = delete;
		GraphicsResourceRetirement& operator=(const GraphicsResourceRetirement&) = delete;

		// 次の描画提出に対応する回収候補を登録する
		void Retire(ComPtr<ID3D12Object> object, BaseDescriptor* descriptor = nullptr, uint32_t index = UINT32_MAX);
		// 登録済み候補へ描画キューの提出Fenceを対応付ける
		void Seal(uint64_t fenceValue);
		// 描画キューの完了済み候補を回収する
		void Collect(uint64_t completedFenceValue);

		//--------- accessor -----------------------------------------------------

		size_t GetPendingCount() const { return pendingCount_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct Entry {

			ComPtr<ID3D12Object> object;
			BaseDescriptor* descriptor = nullptr;
			uint32_t index = UINT32_MAX;
		};
		struct Batch {

			uint64_t fenceValue = 0;
			std::vector<Entry> entries;
		};

		//--------- variables ----------------------------------------------------

		std::vector<Entry> pending_;
		std::deque<Batch> batches_;
		size_t pendingCount_ = 0;
	};
} // Engine
