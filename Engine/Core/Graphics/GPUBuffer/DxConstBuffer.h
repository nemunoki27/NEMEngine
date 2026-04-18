#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/DxUtils.h>

// directX
#include <d3d12.h>
// c++
#include <vector>
#include <cstdint>
#include <optional>
#include <cassert>

namespace Engine {

	//============================================================================
	//	DxConstBuffer class
	//	汎用定数バッファ(CBV)の生成/マップ/転送を行うテンプレートラッパー。
	//============================================================================
	template<typename T>
	class DxConstBuffer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DxConstBuffer() = default;
		~DxConstBuffer() = default;

		// 必要なサイズでCBV用リソースを確保し、永続マップを行う
		void CreateBuffer(ID3D12Device* device);

		// 定数データを即時にマップ領域へコピーする
		void TransferData(const T& data);

		//--------- accessor -----------------------------------------------------

		// 内部リソースを取得する
		ID3D12Resource* GetResource() const { return resource_.Get(); }

		// リソースの作成状態を取得する
		bool IsCreatedResource() const { return isCreated_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_;
		T* mappedData_ = nullptr;

		bool isCreated_ = false;
	};

	//============================================================================
	//	DxConstBuffer templateMethods
	//============================================================================

	template<typename T>
	inline void DxConstBuffer<T>::CreateBuffer(ID3D12Device* device) {

		DxUtils::CreateBufferResource(device, resource_, sizeof(T));

		// マッピング
		HRESULT hr = resource_->Map(0, nullptr, reinterpret_cast<void**>(&mappedData_));
		assert(SUCCEEDED(hr));

		isCreated_ = true;
	}

	template<typename T>
	inline void DxConstBuffer<T>::TransferData(const T& data) {

		if (mappedData_) {

			std::memcpy(mappedData_, &data, sizeof(T));
		}
	}

}; // Engine
