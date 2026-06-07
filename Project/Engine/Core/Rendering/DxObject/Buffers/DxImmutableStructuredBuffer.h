#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/BufferUploadService.h>

// c++
#include <cstdint>
#include <span>

namespace Engine {

	//============================================================================
	//	DxImmutableStructuredBuffer class
	//	ロード後に更新しない静的SRV用の構造化バッファ。DEFAULT heapに本体を置き、
	// 初期データはBufferUploadService経由で1回だけ転送する。CPU Mapは行わない
	// CPU更新が必要な場合はDxStructuredBuffer<T>を使うこと
	//============================================================================
	template<typename T>
	class DxImmutableStructuredBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		DxImmutableStructuredBuffer() = default;
		~DxImmutableStructuredBuffer() = default;

		// DEFAULT heap本体を作成し、初期データ転送をBufferUploadServiceへ依頼する
		void Create(ID3D12Device* device, BufferUploadService& uploadService,
			std::span<const T> data, D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_GENERIC_READ);

		//--------- accessor -----------------------------------------------------

		ID3D12Resource* GetResource() const { return resource_.Get(); }

		// SRVのビュー記述子を生成する
		D3D12_SHADER_RESOURCE_VIEW_DESC GetSRVDesc() const;

		uint32_t GetElementCount() const { return elementCount_; }
		bool IsCreatedResource() const { return resource_ != nullptr; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<ID3D12Resource> resource_;
		uint32_t elementCount_ = 0;
	};

	//============================================================================
	//	DxImmutableStructuredBuffer templateMethods
	//============================================================================
	template<typename T>
	inline void DxImmutableStructuredBuffer<T>::Create(ID3D12Device* device, BufferUploadService& uploadService,
		std::span<const T> data, D3D12_RESOURCE_STATES finalState) {

		// 空配列はバッファを作らない(Buffer Widthが0にならないようにする)
		if (data.empty()) {
			return;
		}

		elementCount_ = static_cast<uint32_t>(data.size());
		const size_t sizeInBytes = sizeof(T) * data.size();

		// DEFAULT heapのバッファは作成時COMMON。コピー用のCOPY_DEST遷移はBufferUploadServiceで積む
		DxUtils::CreateDefaultBufferResource(device, resource_, sizeInBytes);

		uploadService.EnqueueBufferUpload(resource_.Get(), std::as_bytes(data), finalState);
	}

	template<typename T>
	inline D3D12_SHADER_RESOURCE_VIEW_DESC DxImmutableStructuredBuffer<T>::GetSRVDesc() const {

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};

		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Buffer.NumElements = elementCount_;
		srvDesc.Buffer.StructureByteStride = sizeof(T);
		srvDesc.Buffer.FirstElement = 0;

		return srvDesc;
	}
} // Engine

