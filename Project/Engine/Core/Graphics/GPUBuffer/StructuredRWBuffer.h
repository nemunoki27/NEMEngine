#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GPUBuffer/DxStructuredBuffer.h>
#include <Engine/Core/Graphics/Descriptors/SRVDescriptor.h>

// c++
#include <string>
#include <memory>
#include <algorithm>

namespace Engine {

	//============================================================================
	//	StructuredRWBuffer class
	//	SRV/UAV両方のビューを持つStructuredBuffer
	//============================================================================
	template<typename T>
	class StructuredRWBuffer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		StructuredRWBuffer() = default;
		StructuredRWBuffer(const std::string& bindingName) : bindingName_(bindingName) {}
		~StructuredRWBuffer() = default;

		// 初期化
		void Init(ID3D12Device* device, SRVDescriptor* srvDescriptor);

		// 解放
		void Release();

		// 必要な要素数を確保
		void EnsureCapacity(uint32_t requiredCount);

		//--------- accessor -----------------------------------------------------

		// GPUリソースとGPUアドレスを取得する
		ID3D12Resource* GetResource() const { return buffer_->GetResource(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const { return buffer_->GetResource()->GetGPUVirtualAddress(); }
		// SRV/UAVのGPUハンドルを取得する
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSRVGPUHandle() const { return srvGPUHandle_; }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetUAVGPUHandle() const { return uavGPUHandle_; }

		uint32_t GetCapacity() const { return capacity_; }
		uint32_t GetSRVIndex() const { return srvIndex_; }
		std::string_view GetBindingName() const { return bindingName_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;

		// シェーダーバインディング名
		std::string bindingName_{};
		// バッファ
		std::unique_ptr<DxStructuredBuffer<T>> buffer_{};
		// SRV/UAVのGPUハンドル
		D3D12_GPU_DESCRIPTOR_HANDLE srvGPUHandle_{};
		D3D12_GPU_DESCRIPTOR_HANDLE uavGPUHandle_{};

		// 現在の容量
		uint32_t capacity_ = 0;

		// SRV/UAVのインデックス
		uint32_t srvIndex_ = UINT32_MAX;
		uint32_t uavIndex_ = UINT32_MAX;

		//--------- structure ----------------------------------------------------

		uint32_t RoundUpCapacity(uint32_t value) const;
	};

	//============================================================================
	//	StructuredRWBuffer templateMethods
	//============================================================================

	template<typename T>
	inline void StructuredRWBuffer<T>::Init(ID3D12Device* device, SRVDescriptor* srvDescriptor) {

		device_ = device;
		srvDescriptor_ = srvDescriptor;
	}

	template<typename T>
	inline void StructuredRWBuffer<T>::Release() {

		// SRV/UAVを両方解放
		if (srvDescriptor_) {
			if (srvIndex_ != UINT32_MAX) {
				srvDescriptor_->Free(srvIndex_);
				srvIndex_ = UINT32_MAX;
			}
			if (uavIndex_ != UINT32_MAX) {
				srvDescriptor_->Free(uavIndex_);
				uavIndex_ = UINT32_MAX;
			}
		}

		buffer_.reset();
		capacity_ = 0;
		srvGPUHandle_ = {};
		uavGPUHandle_ = {};
	}

	template<typename T>
	inline void StructuredRWBuffer<T>::EnsureCapacity(uint32_t requiredCount) {

		requiredCount = (std::max)(requiredCount, 1u);
		// 十分な容量がある場合は何もしない
		if (requiredCount <= capacity_) {
			return;
		}

		const uint32_t newCapacity = RoundUpCapacity(requiredCount);
		if (srvDescriptor_) {
			if (srvIndex_ != UINT32_MAX) {
				srvDescriptor_->Free(srvIndex_);
				srvIndex_ = UINT32_MAX;
			}
			if (uavIndex_ != UINT32_MAX) {
				srvDescriptor_->Free(uavIndex_);
				uavIndex_ = UINT32_MAX;
			}
		}

		// バッファ作成
		buffer_ = std::make_unique<DxStructuredBuffer<T>>();
		buffer_->CreateUAVBuffer(device_, newCapacity);

		// SRV作成
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = buffer_->GetSRVDesc(newCapacity);
			srvDescriptor_->CreateSRV(srvIndex_, buffer_->GetResource(), srvDesc);
			srvGPUHandle_ = srvDescriptor_->GetGPUHandle(srvIndex_);
			buffer_->SetSRVGPUHandle(srvGPUHandle_);
		}
		// UAV作成
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = buffer_->GetUAVDesc(newCapacity);
			srvDescriptor_->CreateUAV(uavIndex_, buffer_->GetResource(), uavDesc);
			uavGPUHandle_ = srvDescriptor_->GetGPUHandle(uavIndex_);
			buffer_->SetUAVGPUHandle(uavGPUHandle_);
		}
		// 容量更新
		capacity_ = newCapacity;
	}

	template<typename T>
	inline uint32_t StructuredRWBuffer<T>::RoundUpCapacity(uint32_t value) const {

		uint32_t capacity = 64;
		while (capacity < value) {
			capacity *= 2;
		}
		return capacity;
	}
} // Engine