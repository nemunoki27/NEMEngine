#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameResources.h>

// c++
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>
#include <memory>

// directX
#include <d3d12.h>

namespace Engine {

	class BaseDescriptor;
	class DxCommand;
	// 記録中の処理へ、実際に提出したFenceを後から対応付ける
	class GraphicsSubmissionState {
	public:
		uint64_t GetFenceValue() const { return fenceValue_; }
	private:
		friend class DxCommand;
		uint64_t fenceValue_ = 0;
	};
	using GraphicsSubmissionTicket = std::shared_ptr<const GraphicsSubmissionState>;

	// GPUリソース配列が確保する最大フレーム数
	constexpr uint32_t kGraphicsFrameContextCount = 2;
	// 未使用領域を再利用候補として残すframe数
	constexpr uint64_t kGraphicsResourceReuseFrames = 120;

	// 再利用の猶予を過ぎた領域か調べる
	bool HasExpiredGraphicsResource(uint64_t lastUsedSerial, uint64_t frameSerial);

	//============================================================================
	//	GraphicsFrameContext structure
	//============================================================================
	struct GraphicsFrameContext {

		ComPtr<ID3D12CommandAllocator> commandAllocator{};
		uint64_t fenceValue = 0;
		std::shared_ptr<GraphicsSubmissionState> submission;
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

		// 複数資源の登録前に保持領域を確保する
		void ReservePending(size_t additionalCount);
		// 次の描画提出に対応する回収候補を登録する
		void Retire(ComPtr<ID3D12Object> object, BaseDescriptor* descriptor = nullptr, uint32_t index = UINT32_MAX);
		// 登録済み候補へ描画キューの提出Fenceを対応付ける
		void Seal(uint64_t fenceValue);
		// 描画キューの完了済み候補を回収する
		void Collect(uint64_t completedFenceValue);

		// GPU完了後に未使用フレームの資源をその場で回収する
		void ReleaseInactiveFrameResources(uint32_t count);
		GraphicsFrameResourceRegistry& GetFrameResources() { return frameResources_; }

		// Device消失を確認して終了時の保持を解除する
		void ReleaseAfterDeviceRemoval(ID3D12Device* device);

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

		GraphicsFrameResourceRegistry frameResources_{};
		bool immediateRelease_ = false;
		std::vector<Entry> pending_;
		std::deque<Batch> batches_;
		size_t pendingCount_ = 0;
	};
} // Engine
