#include "DxDescriptor.h"
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <string>
#include <string_view>
#include <stdexcept>

//============================================================================
//	BaseDescriptor classMethods
//============================================================================
BaseDescriptor::BaseDescriptor(uint32_t maxDescriptorCount) :
	maxDescriptorCount_(maxDescriptorCount) {
}

void BaseDescriptor::Init(ID3D12Device* device, const DescriptorType& descriptorType) {

	if (maxDescriptorCount_ == 0 || allocatedCount_ != 0) {
		throw std::logic_error("Descriptor容量が0、または使用中のHeapを再初期化しようとしました");
	}

	// 作成失敗時は既存のHeapと管理状態を維持する
	std::vector<uint8_t> flags(maxDescriptorCount_, 0);
	std::vector<std::string> names(maxDescriptorCount_);
	std::vector<uint32_t> freeIndices;
	freeIndices.reserve(maxDescriptorCount_);
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Type = descriptorType.heapType;
	heapDesc.NumDescriptors = maxDescriptorCount_;
	heapDesc.Flags = descriptorType.heapFlags;
	ComPtr<ID3D12DescriptorHeap> heap;
	DxUtils::MakeDescriptorHeap(heap, device, heapDesc);

	// 全確保後に公開し、解放時の追加確保をなくす
	descriptorHeap_ = std::move(heap);
	device_ = device;
	heapType_ = descriptorType.heapType;
	descriptorSize_ = device->GetDescriptorHandleIncrementSize(descriptorType.heapType);
	allocationFlags_ = std::move(flags);
	resourceNames_ = std::move(names);
	freeList_ = std::move(freeIndices);
	useIndex_ = 0;
	allocatedCount_ = 0;
}

D3D12_CPU_DESCRIPTOR_HANDLE BaseDescriptor::GetCPUHandle(uint32_t index) const {

	if (!descriptorHeap_ || index >= maxDescriptorCount_) {
		throw std::out_of_range("Descriptorの参照位置が範囲外です");
	}
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap_.Get()->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (static_cast<uint64_t>(descriptorSize_) * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor::GetGPUHandle(uint32_t index) const {

	if (!descriptorHeap_ || index >= maxDescriptorCount_) {
		throw std::out_of_range("Descriptorの参照位置が範囲外です");
	}
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap_.Get()->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (static_cast<uint64_t>(descriptorSize_) * index);
	return handleGPU;
}

uint32_t BaseDescriptor::Allocate() {

	if (!descriptorHeap_) {
		throw std::logic_error("DescriptorHeapが初期化されていません");
	}
	uint32_t index = UINT32_MAX;

	// フリーリストから再利用
	if (!freeList_.empty()) {

		index = freeList_.back();
		freeList_.pop_back();
	} else {
		if (!DxUtils::CanAllocateIndex(useIndex_, maxDescriptorCount_)) {
			Logger::Output(LogType::Engine, spdlog::level::critical,
				"DescriptorHeapを使い切りました type={} 確保数={} 最大使用位置={} 再利用数={} 上限={}",
				GetHeapTypeName(), allocatedCount_, useIndex_, freeList_.size(), maxDescriptorCount_);
			throw std::length_error("Descriptorをこれ以上確保できません");
		}
		index = useIndex_;
		++useIndex_;
	}

	Assert::Call(index < maxDescriptorCount_, "Descriptorの確保位置が範囲外です");
	Assert::Call(!allocationFlags_[index], "指定位置のDescriptorは既に確保されています");

	allocationFlags_[index] = 1;
	++allocatedCount_;

	return index;

}

void Engine::BaseDescriptor::Free(uint32_t index) {

	if (!IsAllocated(index)) {
		throw std::out_of_range("未確保のDescriptorは解放できません");
	}


	// フリーリストに追加して再利用可能にする
	allocationFlags_[index] = 0;
	resourceNames_[index].clear();
	freeList_.emplace_back(index);

	if (0 < allocatedCount_) {
		--allocatedCount_;
	}
}

bool Engine::BaseDescriptor::IsAllocated(uint32_t index) const {

	if (static_cast<uint32_t>(allocationFlags_.size()) <= index) {
		return false;
	}
	return allocationFlags_[index] != 0;
}

std::string_view Engine::BaseDescriptor::GetResourceName(uint32_t index) const {

	if (static_cast<uint32_t>(resourceNames_.size()) <= index) {
		return {};
	}
	return resourceNames_[index];
}

std::string_view Engine::BaseDescriptor::GetHeapTypeName() const {

	switch (heapType_) {
	case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
		return "CBV_SRV_UAV";
	case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
		return "SAMPLER";
	case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
		return "RTV";
	case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
		return "DSV";
	default:
		break;
	}
	return "UNKNOWN";
}

void Engine::BaseDescriptor::RegisterResourceName(uint32_t index, ID3D12Resource* resource) {

	UpdateResourceName(index, resource);
}

void Engine::BaseDescriptor::UpdateResourceName(uint32_t index, ID3D12Resource* resource) {

	if (static_cast<uint32_t>(resourceNames_.size()) <= index) {
		return;
	}

	resourceNames_[index].clear();
	if (!resource) {
		return;
	}

	UINT nameSize = 0;
	const HRESULT sizeResult = resource->GetPrivateData(WKPDID_D3DDebugObjectNameW, &nameSize, nullptr);
	if ((FAILED(sizeResult) && sizeResult != DXGI_ERROR_MORE_DATA) || nameSize <= sizeof(wchar_t)) {
		return;
	}

	std::wstring wideName(nameSize / sizeof(wchar_t), L'\0');
	if (FAILED(resource->GetPrivateData(WKPDID_D3DDebugObjectNameW, &nameSize, wideName.data()))) {
		return;
	}

	while (!wideName.empty() && wideName.back() == L'\0') {
		wideName.pop_back();
	}
	if (!wideName.empty()) {
		resourceNames_[index] = Algorithm::ConvertString(wideName);
	}
}

void Engine::BaseDescriptor::Retire(uint32_t index, ComPtr<ID3D12Resource> resource) {

	if (!IsAllocated(index)) throw std::out_of_range("未確保のDescriptorは退避できません");
	GetRetirementQueue().Retire(std::move(resource), this, index);
}

Engine::GraphicsResourceRetirement& Engine::BaseDescriptor::GetRetirementQueue() const {

	if (!retirementQueue_) throw std::logic_error("Descriptorの回収窓口が設定されていません");
	return *retirementQueue_;
}
