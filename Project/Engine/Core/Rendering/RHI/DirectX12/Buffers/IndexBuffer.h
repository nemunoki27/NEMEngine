#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RHI/DirectX12/Common/D3D12Utils.h>

// c++
#include <vector>
#include <cassert>

namespace Engine {

	//============================================================================
	//	IndexBuffer class
	//	インデックスバッファ(IB)の作成/転送/ビュー提供を行う軽量ラッパー。
	//============================================================================
	class IndexBuffer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		IndexBuffer() = default;
		virtual ~IndexBuffer() = default;

		// 指定数のインデックス用にリソースを確保し、ビュー情報を初期化する
		void CreateBuffer(ID3D12Device* device, UINT indexCount,
			DXGI_FORMAT format = DXGI_FORMAT_R32_UINT);

		// CPU側のインデックス配列をGPUへ転送する
		void TransferData(const std::vector<uint32_t>& data);
		// 16bit化済みのインデックス配列をGPUへ転送する
		void TransferData(const std::vector<uint16_t>& data);

		//--------- accessor -----------------------------------------------------

		// IBVと内部リソースを取得する
		const D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() const { return indexBufferView_; }
		ID3D12Resource* GetResource() const { return resource_.Get(); }
		DXGI_FORMAT GetFormat() const { return indexBufferView_.Format; }
		uint32_t GetIndexSizeInBytes() const { return indexSizeInBytes_; }

		// リソースの作成状態を取得する
		bool IsCreatedResource() const { return isCreated_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_;
		uint8_t* mappedData_ = nullptr;

		D3D12_INDEX_BUFFER_VIEW indexBufferView_;
		// R16/R32のどちらで確保したかを転送時に参照する
		uint32_t indexSizeInBytes_ = sizeof(uint32_t);

		bool isCreated_ = false;
	};
}; // Engine
