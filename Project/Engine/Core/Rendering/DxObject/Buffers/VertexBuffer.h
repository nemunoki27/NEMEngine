#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxMappedUploadBuffer.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	VertexBuffer class
	// 頂点バッファ(VB)の作成/転送/ビュー提供を行うテンプレートラッパー
	//============================================================================
	template<typename T>
	class VertexBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		VertexBuffer() = default;
		virtual ~VertexBuffer() = default;

		// 指定頂点数でVBリソースを確保し、ビュー情報を初期化する
		void CreateBuffer(ID3D12Device* device, UINT vertexCount);

		// CPU側の頂点配列をGPUへ転送する
		void TransferData(const std::vector<T>& data);

		//--------- accessor -----------------------------------------------------

		// VBVを取得する
		const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const { return vertexBufferView_; }

		// リソースの作成状態を取得する
		bool IsCreatedResource() const { return buffer_.IsCreatedResource(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// UPLOAD heapのマップ済みバッファ
		DxMappedUploadBuffer buffer_;

		D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	};

	//============================================================================
	//	VertexBuffer templateMethods
	//============================================================================
	template<typename T>
	inline void VertexBuffer<T>::CreateBuffer(ID3D12Device* device, UINT vertexCount) {

		if (vertexCount > 0) {

			// 頂点データサイズ
			UINT sizeVB = static_cast<UINT>(sizeof(T) * vertexCount);

			// VBリソースを確保しマップする
			buffer_.Create(device, sizeVB);

			// 頂点バッファビューの作成
			vertexBufferView_.BufferLocation = buffer_.GetGPUVirtualAddress();
			vertexBufferView_.SizeInBytes = sizeVB;
			vertexBufferView_.StrideInBytes = sizeof(T);
		}
	}

	template<typename T>
	inline void VertexBuffer<T>::TransferData(const std::vector<T>& data) {

		buffer_.Write(data.data(), sizeof(T) * data.size());
	}
}; // Engine
