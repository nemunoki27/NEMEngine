#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// directX
#include <d3d12.h>
// c++
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

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

	//============================================================================
	//	GraphicsDeferredReleaseQueue class
	// GPUが参照中のリソースをフレームコンテキスト再利用まで保持する
	//============================================================================
	class GraphicsDeferredReleaseQueue {
	public:
		// 現在フレームで不要になったリソースを遅延解放する
		void Retire(ComPtr<ID3D12Resource> resource) {

			if (!resource) {
				return;
			}
			Collect();
			resources_[GraphicsFrameState::GetCurrentIndex()].emplace_back(
				std::move(resource));
		}

		// 再利用可能になったフレームスロットのリソースを解放する
		void Collect() {

			const uint32_t frameIndex =
				GraphicsFrameState::GetCurrentIndex();
			const uint64_t frameSerial =
				GraphicsFrameState::GetFrameSerial();
			if (frameSerials_[frameIndex] == frameSerial) {
				return;
			}

			resources_[frameIndex].clear();
			frameSerials_[frameIndex] = frameSerial;
		}

		// 保持中の全リソースを解放する
		void Clear() {

			resources_ = {};
			frameSerials_ = {};
		}
	private:
		std::array<std::vector<ComPtr<ID3D12Resource>>,
			kGraphicsFrameContextCount> resources_{};
		std::array<uint64_t,
			kGraphicsFrameContextCount> frameSerials_{};
	};
} // Engine
