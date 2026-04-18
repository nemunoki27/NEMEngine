#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GPUBuffer/DxStructuredBuffer.h>
#include <Engine/Core/Graphics/Descriptors/SRVDescriptor.h>

// c++
#include <string>
#include <vector>
#include <span>

namespace Engine {

	//============================================================================
	//	StructuredInstanceBuffer class
	//	StructuredBufferをインスタンス単位で管理するクラス
	//============================================================================
	template <typename T>
	class StructuredInstanceBuffer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		StructuredInstanceBuffer() = default;
		StructuredInstanceBuffer(const std::string& bindingName) : bindingName_(std::move(bindingName)) {}
		~StructuredInstanceBuffer() = default;

		// 初期化
		void Init(ID3D12Device* device, SRVDescriptor* srvDescriptor);

		// リソース解放
		void Release();

		// データ転送
		void Upload(const std::vector<T>& data);
		void Upload(const std::span<const T>& data);

		// バッファの容量を必要な要素数に合わせて増やす
		void EnsureCapacity(uint32_t requiredCount);

		//--------- accessor -----------------------------------------------------

		// 内部リソースを取得する
		ID3D12Resource* GetResource() const { return buffer_->GetResource(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const { return buffer_->GetResource()->GetGPUVirtualAddress(); }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetGPUHandle() const { return srvGPUHandle_; }

		// 描画バウンディング名を取得する
		std::string_view GetBindingName() const { return bindingName_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;

		// 描画バウンディング名
		std::string bindingName_{};

		// バッファ
		std::unique_ptr<DxStructuredBuffer<T>> buffer_{};
		D3D12_GPU_DESCRIPTOR_HANDLE srvGPUHandle_{};

		// SRVの最大容量
		uint32_t capacity_ = 0;
		uint32_t srvIndex_ = UINT32_MAX;

		//--------- functions ----------------------------------------------------

		// バッファの容量を必要な要素数に合わせて増やす
		uint32_t RoundUpCapacity(uint32_t value) const;
	};

	//============================================================================
	//	StructuredInstanceBuffer templateMethods
	//============================================================================

	template<typename T>
	inline void StructuredInstanceBuffer<T>::Init(ID3D12Device* device, SRVDescriptor* srvDescriptor) {

		device_ = device;
		srvDescriptor_ = srvDescriptor;
	}

	template<typename T>
	inline void StructuredInstanceBuffer<T>::Release() {

		// SRVを解放する
		if (srvDescriptor_ && srvIndex_ != UINT32_MAX) {
			srvDescriptor_->Free(srvIndex_);
			srvIndex_ = UINT32_MAX;
		}
		buffer_.reset();
		capacity_ = 0;
		srvGPUHandle_ = {};
	}

	template<typename T>
	inline void StructuredInstanceBuffer<T>::Upload(const std::vector<T>& data) {

		Upload(std::span<const T>(data.data(), data.size()));
	}

	template<typename T>
	inline void StructuredInstanceBuffer<T>::Upload(const std::span<const T>& data) {

		EnsureCapacity(static_cast<uint32_t>(data.size()));
		if (data.empty()) {
			return;
		}
		buffer_->TransferData(data.data(), static_cast<uint32_t>(data.size()));
	}

	template<typename T>
	inline void StructuredInstanceBuffer<T>::EnsureCapacity(uint32_t requiredCount) {

		requiredCount = (std::max)(requiredCount, 1u);
		// 現在の容量で足りているならなにもしない
		if (requiredCount <= capacity_) {
			return;
		}

		// 新しい容量を計算する
		uint32_t newCapacity = RoundUpCapacity(requiredCount);
		if (srvDescriptor_ && srvIndex_ != UINT32_MAX) {
			srvDescriptor_->Free(srvIndex_);
			srvIndex_ = UINT32_MAX;
		}

		// バッファを作成する
		buffer_ = std::make_unique<DxStructuredBuffer<T>>();
		buffer_->CreateSRVBuffer(device_, newCapacity);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = buffer_->GetSRVDesc(newCapacity);
		srvDescriptor_->CreateSRV(srvIndex_, buffer_->GetResource(), srvDesc);
		srvGPUHandle_ = srvDescriptor_->GetGPUHandle(srvIndex_);
		buffer_->SetSRVGPUHandle(srvGPUHandle_);
		// 容量を更新する
		capacity_ = newCapacity;
	}

	template<typename T>
	inline uint32_t StructuredInstanceBuffer<T>::RoundUpCapacity(uint32_t value) const {
		uint32_t capacity = 64;
		while (capacity < value) {
			capacity *= 2;
		}
		return capacity;
	}
} // Engine