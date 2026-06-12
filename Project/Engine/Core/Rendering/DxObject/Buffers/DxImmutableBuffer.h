#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/BufferUploadService.h>

// c++
#include <cstddef>
#include <span>

namespace Engine {

	//============================================================================
	//	DxImmutableBuffer class
	// 初期化後に更新しない静的DEFAULT heapバッファの共通土台でCPU Mapせずupload service経由で1回だけ転送する
	//============================================================================
	class DxImmutableBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		DxImmutableBuffer() = default;
		~DxImmutableBuffer() = default;

		// DEFAULT heap本体を作成し、初期データ転送をBufferUploadServiceへ依頼する
		void Create(ID3D12Device* device, BufferUploadService& uploadService,
			std::span<const std::byte> data, D3D12_RESOURCE_STATES finalState);

		//--------- accessor -----------------------------------------------------

		// 内部リソースを取得する
		ID3D12Resource* GetResource() const { return resource_.Get(); }
		// GPU仮想アドレスを取得する
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return resource_->GetGPUVirtualAddress(); }

		// リソースの作成状態を取得する
		bool IsCreatedResource() const { return resource_ != nullptr; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_;
	};

	//============================================================================
	//	DxImmutableBuffer methods
	//============================================================================
	inline void DxImmutableBuffer::Create(ID3D12Device* device, BufferUploadService& uploadService,
		std::span<const std::byte> data, D3D12_RESOURCE_STATES finalState) {

		// 空配列はバッファを作らない(Buffer Widthが0にならないようにする)
		if (data.empty()) {
			return;
		}

		// DEFAULT heapのバッファは作成時COMMONでコピー用のCOPY_DEST遷移はBufferUploadServiceで積む
		DxUtils::CreateDefaultBufferResource(device, resource_, data.size());

		uploadService.EnqueueBufferUpload(resource_.Get(), data, finalState);
	}

} // Engine
