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
			const auto& frame = frames_[GraphicsFrameState::GetCurrentIndex()];
			return frame.slots.empty() ? nullptr : frame.slots[frame.current].buffer->GetResource();
		}
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const {
			auto* resource = GetResource();
			return resource ? resource->GetGPUVirtualAddress() : 0;
		}
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetGPUHandle() const {
			return frames_[GraphicsFrameState::GetCurrentIndex()].handle;
		}

		// 描画バウンディング名を取得する
		std::string_view GetBindingName() const { return bindingName_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct Slot {

			std::unique_ptr<DxStructuredBuffer<T>> buffer;
			uint32_t index = UINT32_MAX;
			uint32_t capacity = 0;
			uint64_t lastUsedSerial = UINT64_MAX;
			uint64_t lowUsageSerial = UINT64_MAX;
		};
		struct FrameState {

			std::vector<Slot> slots;
			size_t current = 0;
			uint64_t serial = UINT64_MAX;
			D3D12_GPU_DESCRIPTOR_HANDLE handle{};
		};
		//--------- variables ----------------------------------------------------

		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;
		std::string bindingName_{};
		std::array<FrameState, kGraphicsFrameContextCount> frames_{};

		//--------- functions ----------------------------------------------------

		// 完成したBufferとSRVだけを公開する
		void EnsureSlot(Slot& slot, uint32_t requiredCount);
		// 使用中の組を回収窓口へ渡す
		void RetireSlot(Slot& slot);
	};

	//============================================================================
	//	StructuredInstanceBuffer templateMethods
	//============================================================================
	template<typename T>
	inline void StructuredInstanceBuffer<T>::Init(ID3D12Device* device, SRVDescriptor* srvDescriptor) {

		if (!device || !srvDescriptor || (device_ && (device_ != device || srvDescriptor_ != srvDescriptor))) {
			throw std::logic_error("StructuredBufferのDeviceまたはDescriptorが不正です");
		}
		device_ = device;
		srvDescriptor_ = srvDescriptor;
	}

	template<typename T>
	inline void StructuredInstanceBuffer<T>::Release() {

		for (auto& frame : frames_) {
			for (auto& slot : frame.slots) RetireSlot(slot);
			frame = {};
		}
		device_ = nullptr;
		srvDescriptor_ = nullptr;
	}

	template<typename T>
	inline void StructuredInstanceBuffer<T>::Upload(const std::vector<T>& data) {

		Upload(std::span<const T>(data.data(), data.size()));
	}

	template<typename T>
	inline void StructuredInstanceBuffer<T>::Upload(const std::span<const T>& data) {

		if (data.size() > UINT32_MAX) throw std::length_error("StructuredBufferの要素数が多すぎます");
		auto& frame = frames_[GraphicsFrameState::GetCurrentIndex()];
		const uint64_t serial = GraphicsFrameState::GetFrameSerial();
		const size_t next = frame.serial == serial ? frame.current + 1 : 0;
		// 同じframeの先行描画には別の領域を残す
		if (next == frame.slots.size()) frame.slots.emplace_back();
		EnsureSlot(frame.slots[next], static_cast<uint32_t>(data.size()));
		frame.slots[next].buffer->TransferData(data.data(), data.size());
		frame.current = next;
		frame.serial = serial;
		frame.handle = srvDescriptor_->GetGPUHandle(frame.slots[next].index);
		// 同frameの公開領域を残し、未使用の末尾だけを回収する
		while (frame.slots.size() > frame.current + 1 && HasExpiredGraphicsResource(frame.slots.back().lastUsedSerial, serial)) {
			RetireSlot(frame.slots.back());
			frame.slots.pop_back();
		}
	}

	template<typename T>
	inline void StructuredInstanceBuffer<T>::EnsureCapacity(uint32_t requiredCount) {

		auto& frame = frames_[GraphicsFrameState::GetCurrentIndex()];
		if (frame.slots.empty()) frame.slots.emplace_back();
		EnsureSlot(frame.slots[frame.current], requiredCount);
		frame.handle = srvDescriptor_->GetGPUHandle(frame.slots[frame.current].index);
	}

	template<typename T>
	void StructuredInstanceBuffer<T>::EnsureSlot(Slot& slot, uint32_t requiredCount) {

		if (!device_) throw std::logic_error("StructuredBufferが初期化されていません");
		requiredCount = (std::max)(requiredCount, 1u);
		const uint64_t serial = GraphicsFrameState::GetFrameSerial();
		slot.lastUsedSerial = serial;
		if (slot.capacity >= requiredCount) {
			// 使用量が小さい状態が続いた場合だけ容量を戻す
			if (requiredCount > slot.capacity / 4 || slot.capacity <= 64) {
				slot.lowUsageSerial = UINT64_MAX;
				return;
			}
			if (slot.lowUsageSerial == UINT64_MAX) slot.lowUsageSerial = serial;
			if (!HasExpiredGraphicsResource(slot.lowUsageSerial, serial)) return;
		}
		uint32_t capacity = 64;
		while (capacity < requiredCount) {
			capacity = capacity > UINT32_MAX / 2 ? requiredCount : capacity * 2;
		}
		Slot candidate;
		candidate.buffer = std::make_unique<DxStructuredBuffer<T>>();
		candidate.buffer->CreateSRVBuffer(device_, capacity);
		if (!bindingName_.empty()) {
			candidate.buffer->GetResource()->SetName(Algorithm::ConvertString(bindingName_).c_str());
		}
		srvDescriptor_->CreateSRV(candidate.index, candidate.buffer->GetResource(), candidate.buffer->GetSRVDesc(capacity));
		candidate.capacity = capacity;
		candidate.lastUsedSerial = serial;
		// 未公開のDescriptorは失敗時にその場で返す
		try { RetireSlot(slot); }
		catch (...) { srvDescriptor_->Free(candidate.index); throw; }
		slot = std::move(candidate);
	}

	template<typename T>
	void StructuredInstanceBuffer<T>::RetireSlot(Slot& slot) {

		if (slot.index != UINT32_MAX) {
			ComPtr<ID3D12Resource> resource = slot.buffer->GetResource();
			srvDescriptor_->Retire(slot.index, std::move(resource));
		}
		slot = {};
	}
} // Engine
