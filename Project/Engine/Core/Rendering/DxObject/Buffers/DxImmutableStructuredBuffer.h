#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxImmutableBuffer.h>

// c++
#include <cstdint>
#include <span>
#include <stdexcept>

namespace Engine {

	//============================================================================
	//	DxImmutableStructuredBuffer class
	//	ロード後に更新しない静的SRV用の構造化バッファでDEFAULT heapに本体を置き
	// 初期データはBufferUploadService経由で1回だけ転送しCPU Mapは行わない
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
		DxImmutableStructuredBuffer(const DxImmutableStructuredBuffer&) = delete;
		DxImmutableStructuredBuffer& operator=(const DxImmutableStructuredBuffer&) = delete;
		DxImmutableStructuredBuffer(DxImmutableStructuredBuffer&&) noexcept = default;
		DxImmutableStructuredBuffer& operator=(DxImmutableStructuredBuffer&&) noexcept = default;

		// DEFAULT heap本体を作成し、初期データ転送をBufferUploadServiceへ依頼する
		void Create(ID3D12Device* device, BufferUploadService& uploadService,
			std::span<const T> data, D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_GENERIC_READ);

		//--------- accessor -----------------------------------------------------

		ID3D12Resource* GetResource() const { return buffer_.GetResource(); }

		// SRVのビュー記述子を生成する
		D3D12_SHADER_RESOURCE_VIEW_DESC GetSRVDesc() const;

		uint32_t GetElementCount() const { return elementCount_; }
		bool IsCreatedResource() const { return buffer_.IsCreatedResource(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// DEFAULT heapの静的バッファ
		DxImmutableBuffer buffer_;
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

		if (data.size() > UINT32_MAX) throw std::length_error("静的StructuredBufferの要素数が多すぎます");

		buffer_.Create(device, uploadService, std::as_bytes(data), finalState);
		elementCount_ = static_cast<uint32_t>(data.size());
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
