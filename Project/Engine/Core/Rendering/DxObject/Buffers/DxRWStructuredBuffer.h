#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <string>
#include <memory>
#include <algorithm>
#include <vector>

namespace Engine {

	//============================================================================
	//	StructuredRWBuffer class
	//	SRV/UAV両方のビューを持つStructuredBuffer
	//============================================================================
	template<typename T>
	class StructuredRWBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		StructuredRWBuffer() = default;
		StructuredRWBuffer(const std::string& bindingName) : bindingName_(bindingName) {}
		~StructuredRWBuffer() { Release(); }

		// 初期化
		void Init(ID3D12Device* device, SRVDescriptor* srvDescriptor);

		// 解放
		void Release();

		// 必要な要素数を確保
		void EnsureCapacity(uint32_t requiredCount);

		// GPUリソースの用途stateを遷移
		void Transition(DxCommand& command, D3D12_RESOURCE_STATES nextState);

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
		//============================================================================
		//	private Methods
		//============================================================================

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
		D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_COMMON;

		// SRV/UAVのインデックス
		uint32_t srvIndex_ = UINT32_MAX;
		uint32_t uavIndex_ = UINT32_MAX;

		//--------- functions ----------------------------------------------------

		// 現在のResourceと両Descriptorを回収する
		void RetireBuffer();
		uint32_t RoundUpCapacity(uint32_t value) const;
	};

	//============================================================================
	//	StructuredRWBuffer templateMethods
	//============================================================================
	template<typename T>
	inline void StructuredRWBuffer<T>::Init(ID3D12Device* device, SRVDescriptor* srvDescriptor) {

		if (!device || !srvDescriptor || (device_ && (device_ != device || srvDescriptor_ != srvDescriptor))) {
			throw std::logic_error("RWBufferのDeviceまたはDescriptorが不正です");
		}
		device_ = device;
		srvDescriptor_ = srvDescriptor;
	}

	template<typename T>
	inline void StructuredRWBuffer<T>::Release() {

		RetireBuffer();
		capacity_ = 0;
		currentState_ = D3D12_RESOURCE_STATE_COMMON;
		srvGPUHandle_ = {};
		uavGPUHandle_ = {};
		device_ = nullptr;
		srvDescriptor_ = nullptr;
	}

	template<typename T>
	inline void StructuredRWBuffer<T>::EnsureCapacity(uint32_t requiredCount) {

		requiredCount = (std::max)(requiredCount, 1u);
		// 十分な容量がある場合は何もしない
		if (requiredCount <= capacity_) {
			return;
		}

		if (!device_) throw std::logic_error("RWBufferが初期化されていません");
		const uint32_t newCapacity = RoundUpCapacity(requiredCount);
		auto candidate = std::make_unique<DxStructuredBuffer<T>>();
		candidate->CreateUAVBuffer(device_, newCapacity);
		if (!bindingName_.empty()) candidate->GetResource()->SetName(Algorithm::ConvertString(bindingName_).c_str());
		uint32_t srv = UINT32_MAX;
		uint32_t uav = UINT32_MAX;
		// 両Descriptorの作成が成功するまで旧Bufferを保つ
		try {
			srvDescriptor_->CreateSRV(srv, candidate->GetResource(), candidate->GetSRVDesc(newCapacity));
			srvDescriptor_->CreateUAV(uav, candidate->GetResource(), candidate->GetUAVDesc(newCapacity));
			RetireBuffer();
		} catch (...) {
			if (srv != UINT32_MAX) srvDescriptor_->Free(srv);
			if (uav != UINT32_MAX) srvDescriptor_->Free(uav);
			throw;
		}
		buffer_ = std::move(candidate);
		srvIndex_ = srv;
		uavIndex_ = uav;
		srvGPUHandle_ = srvDescriptor_->GetGPUHandle(srv);
		uavGPUHandle_ = srvDescriptor_->GetGPUHandle(uav);
		capacity_ = newCapacity;
		currentState_ = D3D12_RESOURCE_STATE_COMMON;
	}

	template<typename T>
	void StructuredRWBuffer<T>::RetireBuffer() {

		if (buffer_) srvDescriptor_->GetRetirementQueue().ReservePending(2);
		// 所有元を離れてもResourceとDescriptorを残す
		if (srvIndex_ != UINT32_MAX) {
			srvDescriptor_->Retire(srvIndex_, ComPtr<ID3D12Resource>(buffer_->GetResource()));
			srvIndex_ = UINT32_MAX;
		}
		if (uavIndex_ != UINT32_MAX) {
			srvDescriptor_->Retire(uavIndex_, ComPtr<ID3D12Resource>(buffer_->GetResource()));
			uavIndex_ = UINT32_MAX;
		}
		buffer_.reset();
	}

	template<typename T>
	inline void StructuredRWBuffer<T>::Transition(DxCommand& command, D3D12_RESOURCE_STATES nextState) {

		if (!buffer_ || !buffer_->GetResource() || currentState_ == nextState) {
			return;
		}
		command.TransitionBarriers({ buffer_->GetResource() }, currentState_, nextState);
		currentState_ = nextState;
	}

	template<typename T>
	inline uint32_t StructuredRWBuffer<T>::RoundUpCapacity(uint32_t value) const {

		uint32_t capacity = 64;
		while (capacity < value) {
			capacity = capacity > UINT32_MAX / 2 ? value : capacity * 2;
		}
		return capacity;
	}
} // Engine
