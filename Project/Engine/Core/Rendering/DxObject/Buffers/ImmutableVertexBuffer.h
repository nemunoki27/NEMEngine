#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/BufferUploadService.h>

// c++
#include <span>

namespace Engine {

	//============================================================================
	//	ImmutableVertexBuffer class
	//	初期化後に更新しない静的頂点バッファ。DEFAULT heapに本体を置き、
	// 初期データはBufferUploadService経由で1回だけ転送する。CPU Mapは行わない
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
		ID3D12Resource* GetResource() const { return resource_.Get(); }
		bool IsCreatedResource() const { return resource_ != nullptr; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================
		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_;
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

		// DEFAULT heapのバッファは作成時COMMON。コピー用のCOPY_DEST遷移はBufferUploadServiceで積む
		DxUtils::CreateDefaultBufferResource(device, resource_, sizeInBytes);

		// VBVはDEFAULT heap側のGPUアドレスを指す
		vertexBufferView_.BufferLocation = resource_->GetGPUVirtualAddress();
		vertexBufferView_.SizeInBytes = sizeInBytes;
		vertexBufferView_.StrideInBytes = sizeof(T);

		uploadService.EnqueueBufferUpload(resource_.Get(), std::as_bytes(data), finalState);
	}
} // Engine
