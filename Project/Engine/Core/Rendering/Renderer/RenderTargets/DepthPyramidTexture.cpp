#include "DepthPyramidTexture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// c++
#include <algorithm>

//============================================================================
//	DepthPyramidTexture classMethods
//============================================================================
Engine::DepthPyramidTexture::~DepthPyramidTexture() {

	Destroy();
}

void Engine::DepthPyramidTexture::Create(ID3D12Device* device,
	SRVDescriptor* srvDescriptor, uint32_t width, uint32_t height) {

	Destroy();
	if (!device || !srvDescriptor || width == 0 || height == 0) {
		return;
	}

	srvDescriptor_ = srvDescriptor;
	width_ = width;
	height_ = height;
	mipCount_ = CalculateMipCount(width, height);

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = static_cast<UINT16>(mipCount_);
	resourceDesc.Format = DXGI_FORMAT_R32_FLOAT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	const HRESULT result = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_COMMON, nullptr,
		IID_PPV_ARGS(&resource_));
	if (FAILED(result)) {
		Assert::Call(false,
			"DepthPyramidTexture用リソースの作成に失敗しました");
		Destroy();
		return;
	}
	resource_->SetName(L"SceneDepthPyramid");

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = mipCount_;
	srvDescriptor_->CreateSRV(srvIndex_, resource_.Get(), srvDesc);
	srvGPUHandle_ = srvDescriptor_->GetGPUHandle(srvIndex_);

	mipSRVIndices_.resize(mipCount_, UINT32_MAX);
	mipUAVIndices_.resize(mipCount_, UINT32_MAX);
	mipSRVGPUHandles_.resize(mipCount_);
	mipUAVGPUHandles_.resize(mipCount_);
	mipStates_.resize(mipCount_, D3D12_RESOURCE_STATE_COMMON);
	for (uint32_t mipIndex = 0; mipIndex < mipCount_; ++mipIndex) {

		D3D12_SHADER_RESOURCE_VIEW_DESC mipSRVDesc = srvDesc;
		mipSRVDesc.Texture2D.MostDetailedMip = mipIndex;
		mipSRVDesc.Texture2D.MipLevels = 1;
		srvDescriptor_->CreateSRV(
			mipSRVIndices_[mipIndex],
			resource_.Get(), mipSRVDesc);
		mipSRVGPUHandles_[mipIndex] =
			srvDescriptor_->GetGPUHandle(
				mipSRVIndices_[mipIndex]);

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = mipIndex;
		srvDescriptor_->CreateUAV(
			mipUAVIndices_[mipIndex],
			resource_.Get(), uavDesc);
		mipUAVGPUHandles_[mipIndex] =
			srvDescriptor_->GetGPUHandle(
				mipUAVIndices_[mipIndex]);
	}
}

void Engine::DepthPyramidTexture::Destroy() {

	if (srvDescriptor_) {
		if (srvIndex_ != UINT32_MAX) {
			srvDescriptor_->Retire(srvIndex_, {});
		}
		for (uint32_t index : mipSRVIndices_) {
			if (index != UINT32_MAX) {
				srvDescriptor_->Retire(index, {});
			}
		}
		for (uint32_t index : mipUAVIndices_) {
			if (index != UINT32_MAX) {
				srvDescriptor_->Retire(index, {});
			}
		}
	}

	if (resource_) {
		srvDescriptor_->GetRetirementQueue().Retire(std::move(resource_));
	}
	width_ = 0;
	height_ = 0;
	mipCount_ = 0;
	lastBuiltFrameSerial_ = 0;
	srvIndex_ = UINT32_MAX;
	srvGPUHandle_ = {};
	mipSRVIndices_.clear();
	mipUAVIndices_.clear();
	mipSRVGPUHandles_.clear();
	mipUAVGPUHandles_.clear();
	mipStates_.clear();
	srvDescriptor_ = nullptr;
}

void Engine::DepthPyramidTexture::TransitionMip(
	DxCommand& dxCommand, uint32_t mipIndex,
	D3D12_RESOURCE_STATES newState) {

	if (!resource_ || mipIndex >= mipStates_.size() ||
		mipStates_[mipIndex] == newState) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource_.Get();
	barrier.Transition.StateBefore = mipStates_[mipIndex];
	barrier.Transition.StateAfter = newState;
	barrier.Transition.Subresource = mipIndex;
	dxCommand.GetCommandList()->ResourceBarrier(1, &barrier);
	mipStates_[mipIndex] = newState;
}

uint32_t Engine::DepthPyramidTexture::CalculateMipCount(
	uint32_t width, uint32_t height) {

	uint32_t count = 1;
	uint32_t size = (std::max)(width, height);
	while (size > 1) {
		size /= 2;
		++count;
	}
	return count;
}
