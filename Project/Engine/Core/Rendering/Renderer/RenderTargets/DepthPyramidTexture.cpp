#include "DepthPyramidTexture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

// c++
#include <algorithm>
#include <stdexcept>

//============================================================================
//	DepthPyramidTexture classMethods
//============================================================================
Engine::DepthPyramidTexture::~DepthPyramidTexture() {

	Destroy();
}

void Engine::DepthPyramidTexture::Create(ID3D12Device* device,
	SRVDescriptor* srvDescriptor, uint32_t width, uint32_t height) {

	if (!device || !srvDescriptor || width == 0 || height == 0) {
		throw std::invalid_argument("DepthPyramidTextureの作成条件が不正です");
	}
	DepthPyramidTexture candidate;

	candidate.srvDescriptor_ = srvDescriptor;
	candidate.width_ = width;
	candidate.height_ = height;
	candidate.mipCount_ = CalculateMipCount(width, height);

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = static_cast<UINT16>(candidate.mipCount_);
	resourceDesc.Format = DXGI_FORMAT_R32_FLOAT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	const HRESULT result = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_COMMON, nullptr,
		IID_PPV_ARGS(&candidate.resource_));
	if (!DxDredDiagnostics::CheckHRESULT(device, result, "DepthPyramidTexture::Create")) {
		throw std::runtime_error("DepthPyramidTexture用リソースの作成に失敗しました");
	}
	candidate.resource_->SetName(L"SceneDepthPyramid");

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = candidate.mipCount_;
	candidate.srvDescriptor_->CreateSRV(candidate.srvIndex_, candidate.resource_.Get(), srvDesc);
	candidate.srvGPUHandle_ = candidate.srvDescriptor_->GetGPUHandle(candidate.srvIndex_);

	candidate.mipSRVIndices_.resize(candidate.mipCount_, UINT32_MAX);
	candidate.mipUAVIndices_.resize(candidate.mipCount_, UINT32_MAX);
	candidate.mipSRVGPUHandles_.resize(candidate.mipCount_);
	candidate.mipUAVGPUHandles_.resize(candidate.mipCount_);
	candidate.mipStates_.resize(candidate.mipCount_, D3D12_RESOURCE_STATE_COMMON);
	for (uint32_t mipIndex = 0; mipIndex < candidate.mipCount_; ++mipIndex) {

		D3D12_SHADER_RESOURCE_VIEW_DESC mipSRVDesc = srvDesc;
		mipSRVDesc.Texture2D.MostDetailedMip = mipIndex;
		mipSRVDesc.Texture2D.MipLevels = 1;
		candidate.srvDescriptor_->CreateSRV(
			candidate.mipSRVIndices_[mipIndex],
			candidate.resource_.Get(), mipSRVDesc);
		candidate.mipSRVGPUHandles_[mipIndex] =
			candidate.srvDescriptor_->GetGPUHandle(
				candidate.mipSRVIndices_[mipIndex]);

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = mipIndex;
		candidate.srvDescriptor_->CreateUAV(
			candidate.mipUAVIndices_[mipIndex],
			candidate.resource_.Get(), uavDesc);
		candidate.mipUAVGPUHandles_[mipIndex] =
			candidate.srvDescriptor_->GetGPUHandle(
				candidate.mipUAVIndices_[mipIndex]);
	}

	// 作成途中の失敗では旧描画先を維持する
	Swap(candidate);
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

void Engine::DepthPyramidTexture::Swap(DepthPyramidTexture& other) noexcept {

	std::swap(srvDescriptor_, other.srvDescriptor_);
	std::swap(resource_, other.resource_);
	std::swap(width_, other.width_);
	std::swap(height_, other.height_);
	std::swap(mipCount_, other.mipCount_);
	std::swap(lastBuiltFrameSerial_, other.lastBuiltFrameSerial_);
	std::swap(srvIndex_, other.srvIndex_);
	std::swap(srvGPUHandle_, other.srvGPUHandle_);
	std::swap(mipSRVIndices_, other.mipSRVIndices_);
	std::swap(mipUAVIndices_, other.mipUAVIndices_);
	std::swap(mipSRVGPUHandles_, other.mipSRVGPUHandles_);
	std::swap(mipUAVGPUHandles_, other.mipUAVGPUHandles_);
	std::swap(mipStates_, other.mipStates_);
}
