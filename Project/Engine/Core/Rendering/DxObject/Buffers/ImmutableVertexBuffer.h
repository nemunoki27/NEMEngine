#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxImmutableBuffer.h>

// c++
#include <span>

namespace Engine {

	//============================================================================
	//	ImmutableVertexBuffer class
	//	初期化後に更新しない静的頂点バッファでDEFAULT heapに本体を置き
	// 初期データはBufferUploadService経由で1回だけ転送しCPU Mapは行わない
	// CPU更新が必要な頂点にはVertexBuffer<T>を使うこと
	//============================================================================
	template<typename T>
	class ImmutableVertexBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ImmutableVertexBuffer() = default;
		~ImmutableVertexBuffer() = default;

		// DEFAULT heap本体を作成し、初期データ転送をBufferUploadServiceへ依頼する
		void Create(ID3D12Device* device, BufferUploadService& uploadService,
			std::span<const T> data, D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

		//--------- accessor -----------------------------------------------------

		const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const { return vertexBufferView_; }
		ID3D12Resource* GetResource() const { return buffer_.GetResource(); }
		bool IsCreatedResource() const { return buffer_.IsCreatedResource(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// DEFAULT heapの静的バッファ
		DxImmutableBuffer buffer_;
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	};

	//============================================================================
	//	ImmutableVertexBuffer templateMethods
	//============================================================================
	template<typename T>
	inline void ImmutableVertexBuffer<T>::Create(ID3D12Device* device, BufferUploadService& uploadService,
		std::span<const T> data, D3D12_RESOURCE_STATES finalState) {

		// 空配列はバッファを作らない
		if (data.empty()) {
			return;
		}

		const UINT sizeInBytes = static_cast<UINT>(sizeof(T) * data.size());

		buffer_.Create(device, uploadService, std::as_bytes(data), finalState);

		// VBVはDEFAULT heap側のGPUアドレスを指す
		vertexBufferView_.BufferLocation = buffer_.GetGPUVirtualAddress();
		vertexBufferView_.SizeInBytes = sizeInBytes;
		vertexBufferView_.StrideInBytes = sizeof(T);
	}
} // Engine
