#include "BaseDescriptor.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/DxUtils.h>
#include <Engine/Core/Logger/Assert.h>
#include <Engine/Core/Logger/Logger.h>

// c++
#include <string>

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
				"Descriptor heap exhausted. type={} allocated={} highWater={} freeList={} max={}",
				GetHeapTypeName(), allocatedCount_, useIndex_, freeList_.size(), maxDescriptorCount_);
			Assert::Call(FALSE, std::string("Cannot allocate more DescriptorCount. type=") +
				std::string(GetHeapTypeName()) +
				" allocated=" + std::to_string(allocatedCount_) +
				" highWater=" + std::to_string(useIndex_) +
				" freeList=" + std::to_string(freeList_.size()) +
				" max=" + std::to_string(maxDescriptorCount_));
		}
		index = useIndex_;
		++useIndex_;
	}

	Assert::Call(index < maxDescriptorCount_, "Descriptor index out of range");
	Assert::Call(!allocationFlags_[index], "Descriptor index already allocated");

	allocationFlags_[index] = 1;
	++allocatedCount_;

	return index;

}

void Engine::BaseDescriptor::Free(uint32_t index) {

	Assert::Call(index < maxDescriptorCount_, "Descriptor free index out of range");

	if (!allocationFlags_[index]) {
		Assert::Call(FALSE, "Descriptor double free detected");
		return;
	}

	// フリーリストに追加して再利用可能にする
	allocationFlags_[index] = 0;
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
