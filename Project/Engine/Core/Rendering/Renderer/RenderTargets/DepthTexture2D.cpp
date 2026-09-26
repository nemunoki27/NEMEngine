#include "DepthTexture2D.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Descriptors/DxDepthStencilView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

// c++
#include <stdexcept>

//============================================================================
//	DepthTexture2D classMethods
//============================================================================
Engine::DepthTexture2D::~DepthTexture2D() {

	Destroy();
}

void Engine::DepthTexture2D::Create(DSVDescriptor* dsvDescriptor,
	SRVDescriptor* srvDescriptor, const DepthTextureCreateDesc& desc) {

	if (!dsvDescriptor || !srvDescriptor || desc.width == 0 || desc.height == 0) {
		throw std::invalid_argument("DepthTexture2Dの作成条件が不正です");
	}
	DepthTexture2D candidate;

	candidate.dsvDescriptor_ = dsvDescriptor;
	candidate.srvDescriptor_ = srvDescriptor;

	// サイズ設定
	candidate.width_ = desc.width;
	candidate.height_ = desc.height;
	candidate.resourceFormat_ = desc.resourceFormat;
	candidate.dsvFormat_ = desc.dsvFormat;
	candidate.srvFormat_ = desc.srvFormat;

	// DSVを作成し、CPUハンドルを保存する
	dsvDescriptor->CreateDSV(desc.width, desc.height, candidate.dsvIndex_,
		candidate.dsvCPUHandle_, candidate.resource_, desc.resourceFormat, desc.dsvFormat);
	if (!desc.debugName.empty()) {

		candidate.resource_->SetName(desc.debugName.c_str());
		dsvDescriptor->UpdateResourceName(candidate.dsvIndex_, candidate.resource_.Get());
	}

	// SRVの作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = desc.srvFormat;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDescriptor->CreateSRV(candidate.srvIndex_, candidate.resource_.Get(), srvDesc);
	candidate.srvGPUHandle_ = srvDescriptor->GetGPUHandle(candidate.srvIndex_);

	// 現在のリソース状態を深度書き込みに設定する
	candidate.currentState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

	// 作成途中の失敗では旧描画先を維持する
	Swap(candidate);
}

void Engine::DepthTexture2D::Destroy() {

	if (dsvDescriptor_ && dsvIndex_ != UINT32_MAX) {

		dsvDescriptor_->Retire(dsvIndex_, {});
	}
	if (srvDescriptor_ && srvIndex_ != UINT32_MAX) {

		srvDescriptor_->Retire(srvIndex_, {});
	}
	if (resource_) {
		srvDescriptor_->GetRetirementQueue().Retire(std::move(resource_));
	}
	dsvCPUHandle_ = {};
	srvGPUHandle_ = {};
	width_ = 0;
	height_ = 0;
	currentState_ = D3D12_RESOURCE_STATE_COMMON;
	resourceFormat_ = DXGI_FORMAT_UNKNOWN;
	dsvFormat_ = DXGI_FORMAT_UNKNOWN;
	srvFormat_ = DXGI_FORMAT_UNKNOWN;
	dsvIndex_ = UINT32_MAX;
	srvIndex_ = UINT32_MAX;
	dsvDescriptor_ = nullptr;
	srvDescriptor_ = nullptr;
}

void Engine::DepthTexture2D::Transition(DxCommand& dxCommand, D3D12_RESOURCE_STATES newState) {

	// リソースが存在していない場合や、現在の状態と遷移先の状態が同じ場合は遷移しない
	if (!resource_ || currentState_ == newState) {
		return;
	}
	dxCommand.TransitionBarriers({ resource_.Get() }, currentState_, newState);
	currentState_ = newState;
}

void Engine::DepthTexture2D::Swap(DepthTexture2D& other) noexcept {

	std::swap(dsvDescriptor_, other.dsvDescriptor_);
	std::swap(srvDescriptor_, other.srvDescriptor_);
	std::swap(resource_, other.resource_);
	std::swap(dsvCPUHandle_, other.dsvCPUHandle_);
	std::swap(srvGPUHandle_, other.srvGPUHandle_);
	std::swap(width_, other.width_);
	std::swap(height_, other.height_);
	std::swap(currentState_, other.currentState_);
	std::swap(resourceFormat_, other.resourceFormat_);
	std::swap(dsvFormat_, other.dsvFormat_);
	std::swap(srvFormat_, other.srvFormat_);
	std::swap(dsvIndex_, other.dsvIndex_);
	std::swap(srvIndex_, other.srvIndex_);
}
