#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

// c++
#include <array>

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
		void Init(ID3D12Device* device);

		// データ転送
		void Upload(const T& value);

		//--------- accessor -----------------------------------------------------

		// 内部リソースを取得する
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const {
			return buffers_[GraphicsFrameState::GetCurrentIndex()]
				.GetResource()->GetGPUVirtualAddress();
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
		std::array<DxConstBuffer<T>, kGraphicsFrameContextCount> buffers_{};
	};

	//============================================================================
	//	ViewConstantBuffer templateMethods
	//============================================================================
	template<typename T>
	inline void ViewConstantBuffer<T>::Init(ID3D12Device* device) {

		for (DxConstBuffer<T>& buffer : buffers_) {

			if (!buffer.IsCreatedResource()) {
				buffer.CreateBuffer(device);
			}
		}
	}

	template<typename T>
	inline void ViewConstantBuffer<T>::Upload(const T& value) {

		buffers_[GraphicsFrameState::GetCurrentIndex()].TransferData(value);
	}
} // Engine
