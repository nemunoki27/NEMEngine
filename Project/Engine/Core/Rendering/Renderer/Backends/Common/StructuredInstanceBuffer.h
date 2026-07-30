#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <array>
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
		//============================================================================
		//	public Methods
		//============================================================================

		StructuredInstanceBuffer() = default;
		StructuredInstanceBuffer(const std::string& bindingName) : bindingName_(std::move(bindingName)) {}
		~StructuredInstanceBuffer() { Release(); }

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
		ID3D12Resource* GetResource() const {
			return buffers_[GraphicsFrameState::GetCurrentIndex()]->GetResource();
		}
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const {
			return GetResource()->GetGPUVirtualAddress();
		}
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetGPUHandle() const {
			return srvGPUHandles_[GraphicsFrameState::GetCurrentIndex()];
		}

		// 描画バウンディング名を取得する
		std::string_view GetBindingName() const { return bindingName_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;

		// 描画バウンディング名
		std::string bindingName_{};

		// バッファ
		std::array<std::unique_ptr<DxStructuredBuffer<T>>,
			kGraphicsFrameContextCount> buffers_{};
		std::array<D3D12_GPU_DESCRIPTOR_HANDLE,
			kGraphicsFrameContextCount> srvGPUHandles_{};

		// SRVの最大容量
		uint32_t capacity_ = 0;
		std::array<uint32_t, kGraphicsFrameContextCount> srvIndices_ = {
			UINT32_MAX, UINT32_MAX, UINT32_MAX
		};
		// 容量拡張前のリソースとDescriptorはGPU完了前に破棄しない
		std::vector<std::unique_ptr<DxStructuredBuffer<T>>> retiredBuffers_{};
		std::vector<uint32_t> retiredSrvIndices_{};

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
		if (srvDescriptor_) {
			for (uint32_t& index : srvIndices_) {
				if (index != UINT32_MAX) {
					srvDescriptor_->Free(index);
					index = UINT32_MAX;
				}
			}
			for (uint32_t index : retiredSrvIndices_) {
				srvDescriptor_->Free(index);
			}
		}
		for (std::unique_ptr<DxStructuredBuffer<T>>& buffer : buffers_) {
			buffer.reset();
		}
		retiredBuffers_.clear();
		retiredSrvIndices_.clear();
		capacity_ = 0;
		srvGPUHandles_ = {};
		device_ = nullptr;
		srvDescriptor_ = nullptr;
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
		buffers_[GraphicsFrameState::GetCurrentIndex()]->TransferData(
			data.data(), static_cast<uint32_t>(data.size()));
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
		for (uint32_t frameIndex = 0;
			frameIndex < kGraphicsFrameContextCount; ++frameIndex) {

			if (buffers_[frameIndex]) {
				retiredBuffers_.emplace_back(std::move(buffers_[frameIndex]));
			}
			if (srvIndices_[frameIndex] != UINT32_MAX) {
				retiredSrvIndices_.emplace_back(srvIndices_[frameIndex]);
				srvIndices_[frameIndex] = UINT32_MAX;
			}

			// 各フレームでCPU更新領域を分離する
			buffers_[frameIndex] = std::make_unique<DxStructuredBuffer<T>>();
			buffers_[frameIndex]->CreateSRVBuffer(device_, newCapacity);
			if (!bindingName_.empty()) {
				const std::string resourceName = bindingName_ +
					"[" + std::to_string(frameIndex) + "]";
				buffers_[frameIndex]->GetResource()->SetName(
					Algorithm::ConvertString(resourceName).c_str());
			}
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc =
				buffers_[frameIndex]->GetSRVDesc(newCapacity);
			srvDescriptor_->CreateSRV(srvIndices_[frameIndex],
				buffers_[frameIndex]->GetResource(), srvDesc);
			srvGPUHandles_[frameIndex] =
				srvDescriptor_->GetGPUHandle(srvIndices_[frameIndex]);
			buffers_[frameIndex]->SetSRVGPUHandle(
				srvGPUHandles_[frameIndex]);
		}
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
