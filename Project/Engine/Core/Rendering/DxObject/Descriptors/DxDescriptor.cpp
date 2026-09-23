#include "DxDescriptor.h"
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <string>
#include <string_view>

//============================================================================
//	BaseDescriptor classMethods
//============================================================================
BaseDescriptor::BaseDescriptor(uint32_t maxDescriptorCount) :
	maxDescriptorCount_(maxDescriptorCount) {
}

void BaseDescriptor::Init(ID3D12Device* device, const DescriptorType& descriptorType) {

	device_ = device;
	heapType_ = descriptorType.heapType;
	useIndex_ = 0;
	allocatedCount_ = 0;
	freeList_.clear();
	allocationFlags_.assign(maxDescriptorCount_, 0);
	resourceNames_.assign(maxDescriptorCount_, {});


	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Type = descriptorType.heapType;
	heapDesc.NumDescriptors = maxDescriptorCount_;
	heapDesc.Flags = descriptorType.heapFlags;

	// デスクリプタ生成
	DxUtils::MakeDescriptorHeap(descriptorHeap_, device, heapDesc);
	descriptorSize_ = device->GetDescriptorHandleIncrementSize(descriptorType.heapType);
}

D3D12_CPU_DESCRIPTOR_HANDLE BaseDescriptor::GetCPUHandle(uint32_t index) const {

	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap_.Get()->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize_ * index);
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE BaseDescriptor::GetGPUHandle(uint32_t index) const {

	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap_.Get()->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize_ * index);
	return handleGPU;
}

uint32_t BaseDescriptor::Allocate() {

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
			Assert::Call(FALSE, std::string("Descriptorをこれ以上確保できません type=") +
				std::string(GetHeapTypeName()) +
				" 確保数=" + std::to_string(allocatedCount_) +
				" 最大使用位置=" + std::to_string(useIndex_) +
				" 再利用数=" + std::to_string(freeList_.size()) +
				" 上限=" + std::to_string(maxDescriptorCount_));
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

	Assert::Call(index < maxDescriptorCount_, "Descriptorの解放位置が範囲外です");

	if (!allocationFlags_[index]) {
		Assert::Call(FALSE, "Descriptorの二重解放を検出しました");
		return;
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

	Assert::Call(IsAllocated(index), "未確保のDescriptorは退避できません");
	GetRetirementQueue().Retire(std::move(resource), this, index);
}

Engine::GraphicsResourceRetirement& Engine::BaseDescriptor::GetRetirementQueue() const {

	Assert::Call(retirementQueue_ != nullptr, "Descriptorの回収窓口が設定されていません");
	return *retirementQueue_;
}
