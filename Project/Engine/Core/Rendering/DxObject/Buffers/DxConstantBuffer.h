#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxMappedUploadBuffer.h>

namespace Engine {

	//============================================================================
	//	DxConstBuffer class
	// 汎用定数バッファ(CBV)の生成/マップ/転送を行うテンプレートラッパー
	//============================================================================
	template<typename T>
	class DxConstBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DxConstBuffer() = default;
		~DxConstBuffer() = default;

		// 必要なサイズでCBV用リソースを確保し、永続マップを行う
		void CreateBuffer(GraphicsResourceRetirement& retirement, ID3D12Device* device);

		// 定数データを即時にマップ領域へコピーする
		void TransferData(const T& data);

		//--------- accessor -----------------------------------------------------

		// 内部リソースを取得する
		ID3D12Resource* GetResource() const { return buffer_.GetResource(); }

		// リソースの作成状態を取得する
		bool IsCreatedResource() const { return buffer_.IsCreatedResource(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// UPLOAD heapのマップ済みバッファ
		DxMappedUploadBuffer buffer_;
	};

	//============================================================================
	//	DxConstBuffer templateMethods
	//============================================================================
	template<typename T>
	inline void DxConstBuffer<T>::CreateBuffer(GraphicsResourceRetirement& retirement, ID3D12Device* device) {

		buffer_.Create(retirement, device, sizeof(T));
	}

	template<typename T>
	inline void DxConstBuffer<T>::TransferData(const T& data) {

		buffer_.Write(&data, sizeof(T));
	}

}; // Engine
