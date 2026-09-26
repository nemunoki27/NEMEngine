#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/BufferUploadService.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Engine {

	//============================================================================
	//	DefaultStructuredInstanceBuffer class
	//	構造化データをDEFAULT heapへ保持してフレームごとの差分を転送する
	//============================================================================
	template<typename T>
	class DefaultStructuredInstanceBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DefaultStructuredInstanceBuffer() = default;
		explicit DefaultStructuredInstanceBuffer(std::string bindingName) :
			bindingName_(std::move(bindingName)) {
		}
		~DefaultStructuredInstanceBuffer() { Release(); }

		// 初期化
		void Init(ID3D12Device* device, SRVDescriptor* srvDescriptor,
			BufferUploadService* uploadService);
		// GPU利用中の旧リソースを保持したまま現在の参照を解放する
		void Release();

		// 全要素を更新対象にする
		void MarkFullUpdate(uint32_t elementCount);
		// 指定範囲だけを更新対象にする
		void MarkDirtyRange(uint32_t firstElement, uint32_t elementCount);
		// 現在のFrame Contextが未反映の差分をDEFAULT heapへ転送する
		size_t UploadCurrentFrame(std::span<const T> data);

		//--------- accessor -----------------------------------------------------

		ID3D12Resource* GetResource() const {
			const uint32_t index = GraphicsFrameState::GetCurrentIndex();
			return transientActive_[index] ? transient_.GetResource() : resources_[index].Get();
		}
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const {
			ID3D12Resource* resource = GetResource();
			return resource ? resource->GetGPUVirtualAddress() : 0;
		}
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetGPUHandle() const {
			const uint32_t index = GraphicsFrameState::GetCurrentIndex();
			return transientActive_[index] ? transient_.GetGPUHandle() : srvGPUHandles_[index];
		}
		std::string_view GetBindingName() const { return bindingName_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct DirtyRange {

			uint64_t generation = 0;
			uint32_t firstElement = 0;
			uint32_t elementCount = 0;
		};

		//--------- variables ----------------------------------------------------

		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;
		BufferUploadService* uploadService_ = nullptr;

		std::string bindingName_{};
		std::array<ComPtr<ID3D12Resource>,
			kGraphicsFrameContextCount> resources_{};
		std::array<D3D12_GPU_DESCRIPTOR_HANDLE,
			kGraphicsFrameContextCount> srvGPUHandles_{};
		std::array<uint32_t, kGraphicsFrameContextCount> srvIndices_ = {
			UINT32_MAX, UINT32_MAX, UINT32_MAX
		};
		std::array<uint64_t, kGraphicsFrameContextCount>
			uploadedGenerations_ = { 0, 0, 0 };

		// 同frameの再更新だけ別領域へ転送する
		StructuredInstanceBuffer<T> transient_;
		std::array<bool, kGraphicsFrameContextCount> transientActive_{};
		std::array<uint64_t, kGraphicsFrameContextCount> publishedSerials_{ UINT64_MAX, UINT64_MAX, UINT64_MAX };
		std::array<uint64_t, kGraphicsFrameContextCount> publishedGenerations_{};
		std::array<size_t, kGraphicsFrameContextCount> publishedCounts_{};
		std::array<size_t, kGraphicsFrameContextCount> uploadedCounts_{};
		uint32_t capacity_ = 0;
		uint32_t elementCount_ = 0;
		uint64_t generation_ = 1;
		std::deque<DirtyRange> dirtyRanges_{};

		//--------- functions ----------------------------------------------------

		// 全フレームの容量を必要な要素数まで拡張する
		void EnsureCapacity(uint32_t requiredCount);
		// 現在の資源とDescriptorを描画完了まで預ける
		void RetireFrame(uint32_t frameIndex);
		// 全フレームへ反映済みの変更範囲を除く
		void PruneDirtyRanges();
		// 確保容量を段階的に切り上げる
		static uint32_t RoundUpCapacity(uint32_t value);
	};

	//============================================================================
	//	DefaultStructuredInstanceBuffer templateMethods
	//============================================================================
	template<typename T>
	void DefaultStructuredInstanceBuffer<T>::Init(
		ID3D12Device* device, SRVDescriptor* srvDescriptor,
		BufferUploadService* uploadService) {

		if (!device || !srvDescriptor || !uploadService || (device_ &&
			(device_ != device || srvDescriptor_ != srvDescriptor || uploadService_ != uploadService))) {
			throw std::logic_error("差分Bufferの初期化条件が不正です");
		}
		transient_.Init(device, srvDescriptor);
		device_ = device;
		srvDescriptor_ = srvDescriptor;
		uploadService_ = uploadService;
	}

	template<typename T>
	void DefaultStructuredInstanceBuffer<T>::Release() {

		for (uint32_t frameIndex = 0; frameIndex < kGraphicsFrameContextCount; ++frameIndex) {
			RetireFrame(frameIndex);
		}
		transient_.Release();
		transientActive_ = {};
		publishedSerials_.fill(UINT64_MAX);
		publishedGenerations_ = {};
		publishedCounts_ = {};
		uploadedCounts_ = {};
		srvGPUHandles_ = {};
		uploadedGenerations_ = { 0, 0, 0 };
		dirtyRanges_.clear();
		capacity_ = 0;
		elementCount_ = 0;
		generation_ = 1;
		uploadService_ = nullptr;
		srvDescriptor_ = nullptr;
		device_ = nullptr;
	}

	template<typename T>
	void DefaultStructuredInstanceBuffer<T>::MarkFullUpdate(
		uint32_t elementCount) {

		elementCount_ = elementCount;
		MarkDirtyRange(0, elementCount);
	}

	template<typename T>
	void DefaultStructuredInstanceBuffer<T>::MarkDirtyRange(
		uint32_t firstElement, uint32_t elementCount) {

		if (elementCount == 0) {
			return;
		}

		if (firstElement > UINT32_MAX - elementCount) throw std::out_of_range("差分Bufferの更新範囲が大きすぎます");
		const uint64_t next = generation_ == UINT64_MAX ? 1 : generation_ + 1;
		const DirtyRange range{ next, firstElement, elementCount };
		if (generation_ == UINT64_MAX) {
			std::deque<DirtyRange> replacement{ range };
			dirtyRanges_.swap(replacement);
			uploadedGenerations_ = {};
			publishedGenerations_ = {};
		} else {
			dirtyRanges_.push_back(range);
		}
		// 変更範囲を保持できてから世代を進める
		generation_ = next;
	}

	template<typename T>
	size_t DefaultStructuredInstanceBuffer<T>::UploadCurrentFrame(
		std::span<const T> data) {

		if (data.size() > UINT32_MAX) throw std::length_error("差分Bufferの要素数が多すぎます");
		const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
		const uint64_t serial = GraphicsFrameState::GetFrameSerial();
		if (publishedSerials_[frameIndex] == serial) {
			if (publishedGenerations_[frameIndex] == generation_ && publishedCounts_[frameIndex] == data.size()) return 0;
			// 先行描画のDEFAULT領域には上書きしない
			transient_.Upload(data);
			transientActive_[frameIndex] = true;
			publishedGenerations_[frameIndex] = generation_;
			publishedCounts_[frameIndex] = data.size();
			return data.size_bytes();
		}
		elementCount_ = static_cast<uint32_t>(data.size());
		EnsureCapacity(elementCount_);
		if (data.empty()) return 0;
		uint64_t& uploadedGeneration = uploadedGenerations_[frameIndex];
		if (uploadedGeneration == generation_ && uploadedCounts_[frameIndex] == data.size()) {
			transientActive_[frameIndex] = false;
			publishedSerials_[frameIndex] = serial;
			publishedGenerations_[frameIndex] = generation_;
			publishedCounts_[frameIndex] = data.size();
			return 0;
		}

		uint32_t firstElement = 0;
		uint32_t endElement = elementCount_;
		bool foundRange = uploadedGeneration == 0 || uploadedCounts_[frameIndex] != data.size();
		if (!foundRange) {
			firstElement = elementCount_;
			endElement = 0;
			for (const DirtyRange& range : dirtyRanges_) {
				if (range.generation <= uploadedGeneration) {
					continue;
				}
				firstElement = (std::min)(
					firstElement, range.firstElement);
				endElement = (std::max)(endElement,
					range.firstElement + range.elementCount);
				foundRange = true;
			}
		}
		if (!foundRange) {
			firstElement = 0;
			endElement = elementCount_;
		}

		firstElement = (std::min)(firstElement, elementCount_);
		endElement = (std::min)(endElement, elementCount_);
		if (firstElement < endElement) {

			const std::span<const T> source =
				data.subspan(firstElement,
					endElement - firstElement);
			uploadService_->EnqueueBufferUpload(
				resources_[frameIndex].Get(),
				static_cast<size_t>(firstElement) * sizeof(T),
				std::as_bytes(source),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				D3D12_RESOURCE_STATE_GENERIC_READ);
		}
		uploadedGeneration = generation_;
		uploadedCounts_[frameIndex] = data.size();
		transientActive_[frameIndex] = false;
		publishedSerials_[frameIndex] = serial;
		publishedGenerations_[frameIndex] = generation_;
		publishedCounts_[frameIndex] = data.size();
		PruneDirtyRanges();
		return endElement > firstElement ? static_cast<size_t>(endElement - firstElement) * sizeof(T) : 0;
	}

	template<typename T>
	void DefaultStructuredInstanceBuffer<T>::EnsureCapacity(
		uint32_t requiredCount) {

		requiredCount = (std::max)(requiredCount, 1u);
		if (requiredCount <= capacity_) {
			return;
		}

		const uint32_t newCapacity = RoundUpCapacity(requiredCount);
		std::array<ComPtr<ID3D12Resource>, kGraphicsFrameContextCount> candidates;
		std::array<uint32_t, kGraphicsFrameContextCount> indices;
		indices.fill(UINT32_MAX);
		// 全frameのResourceとSRVを作成してから交換する
		try {
			for (uint32_t index = 0; index < kGraphicsFrameContextCount; ++index) {
				DxUtils::CreateDefaultBufferResource(device_, candidates[index], static_cast<size_t>(newCapacity) * sizeof(T));
				if (!bindingName_.empty()) candidates[index]->SetName(Algorithm::ConvertString(bindingName_).c_str());
				D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
				desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
				desc.Format = DXGI_FORMAT_UNKNOWN;
				desc.Buffer.NumElements = newCapacity;
				desc.Buffer.StructureByteStride = sizeof(T);
				srvDescriptor_->CreateSRV(indices[index], candidates[index].Get(), desc);
			}
			srvDescriptor_->GetRetirementQueue().ReservePending(kGraphicsFrameContextCount);
		} catch (...) {
			for (uint32_t index : indices) if (index != UINT32_MAX) srvDescriptor_->Free(index);
			throw;
		}
		for (uint32_t index = 0; index < kGraphicsFrameContextCount; ++index) RetireFrame(index);
		resources_ = std::move(candidates);
		srvIndices_ = indices;
		for (uint32_t index = 0; index < kGraphicsFrameContextCount; ++index) {
			srvGPUHandles_[index] = srvDescriptor_->GetGPUHandle(indices[index]);
		}
		capacity_ = newCapacity;
		uploadedGenerations_ = {};
	}

	template<typename T>
	void DefaultStructuredInstanceBuffer<T>::RetireFrame(uint32_t frameIndex) {

		if (srvIndices_[frameIndex] != UINT32_MAX) {
			srvDescriptor_->Retire(srvIndices_[frameIndex], std::move(resources_[frameIndex]));
			srvIndices_[frameIndex] = UINT32_MAX;
		}
	}

	template<typename T>
	void DefaultStructuredInstanceBuffer<T>::PruneDirtyRanges() {

		uint64_t oldestRequiredGeneration = generation_;
		for (uint64_t uploaded : uploadedGenerations_) {
			if (uploaded == 0) {
				continue;
			}
			oldestRequiredGeneration = (std::min)(
				oldestRequiredGeneration, uploaded);
		}
		while (!dirtyRanges_.empty() &&
			dirtyRanges_.front().generation <=
			oldestRequiredGeneration) {
			dirtyRanges_.pop_front();
		}
	}

	template<typename T>
	uint32_t DefaultStructuredInstanceBuffer<T>::RoundUpCapacity(
		uint32_t value) {

		uint32_t capacity = 64;
		while (capacity < value) {
			capacity = capacity > UINT32_MAX / 2 ? value : capacity * 2;
		}
		return capacity;
	}
} // Engine
