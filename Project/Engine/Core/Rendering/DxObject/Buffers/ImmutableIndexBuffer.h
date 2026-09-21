#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxImmutableBuffer.h>

// c++
#include <cstdint>
#include <span>

namespace Engine {

	//============================================================================
	//	ImmutableIndexBuffer class
	//	初期化後に更新しない静的インデックスバッファでDEFAULT heapに本体を置き
	// 初期データはBufferUploadService経由で1回だけ転送し16bit/32bit両対応
	// CPU更新が必要なIndexにはIndexBufferを使うこと
	//============================================================================
	class ImmutableIndexBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ImmutableIndexBuffer() = default;
		~ImmutableIndexBuffer() = default;

		// 32bitインデックスでDEFAULT heap本体を作成し、転送を依頼する
		// BLAS入力など別用途でも読む場合はfinalStateにGENERIC_READを指定する
		void Create(ID3D12Device* device, BufferUploadService& uploadService,
			std::span<const uint32_t> data, DXGI_FORMAT format = DXGI_FORMAT_R32_UINT,
			D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_INDEX_BUFFER);

		// 16bitインデックスでDEFAULT heap本体を作成し、転送を依頼する
		void Create(ID3D12Device* device, BufferUploadService& uploadService,
			std::span<const uint16_t> data,
			D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_INDEX_BUFFER);

		//--------- accessor -----------------------------------------------------

		const D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() const { return indexBufferView_; }
		ID3D12Resource* GetResource() const { return buffer_.GetResource(); }
		DXGI_FORMAT GetFormat() const { return indexBufferView_.Format; }
		// 1インデックスのバイト数(R16なら2、R32なら4)
		uint32_t GetIndexSizeInBytes() const;
		bool IsCreatedResource() const { return buffer_.IsCreatedResource(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// DEFAULT heapの静的バッファ
		DxImmutableBuffer buffer_;
		D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
	};
} // Engine
