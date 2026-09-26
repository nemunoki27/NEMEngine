#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

// c++
#include <array>
#include <string>
#include <string_view>
#include <stdexcept>

namespace Engine {

	//============================================================================
	//	ViewConstantBuffer class
	//	描画ビューに関する定数バッファを管理するクラス
	//============================================================================
	template <typename T>
	class ViewConstantBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ViewConstantBuffer() = default;
		ViewConstantBuffer(const std::string& bindingName) : bindingName_(std::move(bindingName)) {}
		~ViewConstantBuffer() = default;

		// 初期化
		void Init(GraphicsResourceRetirement& retirement, ID3D12Device* device);

		// 使用中の定数領域を回収窓口へ渡す
		void Release();

		// データ転送
		void Upload(const T& value);

		//--------- accessor -----------------------------------------------------

		// 内部リソースを取得する
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const {
			return addresses_[GraphicsFrameState::GetCurrentIndex()];
		}

		// 描画バウンディング名を取得する
		std::string_view GetBindingName() const { return bindingName_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 描画バウンディング名
		std::string bindingName_{};

		// バッファ
		FrameConstantBufferAllocator allocator_{ sizeof(T) };
		std::array<D3D12_GPU_VIRTUAL_ADDRESS, kGraphicsFrameContextCount> addresses_{};
		ID3D12Device* device_ = nullptr;
		GraphicsResourceRetirement* retirement_ = nullptr;
	};

	//============================================================================
	//	ViewConstantBuffer templateMethods
	//============================================================================
	template<typename T>
	inline void ViewConstantBuffer<T>::Init(GraphicsResourceRetirement& retirement, ID3D12Device* device) {

		if (!device || (device_ && (device_ != device || retirement_ != &retirement))) {
			throw std::logic_error("View定数BufferのDeviceまたは回収先が不正です");
		}
		if (device_) return;
		device_ = device;
		retirement_ = &retirement;
	}

	template<typename T>
	inline void ViewConstantBuffer<T>::Release() {

		allocator_.Release();
		addresses_ = {};
		device_ = nullptr;
		retirement_ = nullptr;
	}

	template<typename T>
	inline void ViewConstantBuffer<T>::Upload(const T& value) {

		if (!retirement_) throw std::logic_error("View定数Bufferが初期化されていません");
		// 同じframeの再転送でも先の描画領域を維持する
		addresses_[GraphicsFrameState::GetCurrentIndex()] = allocator_.AllocateAndUpload(*retirement_, device_, value).gpuAddress;
	}
} // Engine
