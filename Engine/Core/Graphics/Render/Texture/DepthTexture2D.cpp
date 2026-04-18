#include "DepthTexture2D.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Descriptors/DSVDescriptor.h>
#include <Engine/Core/Graphics/Descriptors/SRVDescriptor.h>
#include <Engine/Core/Graphics/DxObject/DxCommand.h>
#include <Engine/Debug/Assert.h>

//============================================================================
//	DepthTexture2D classMethods
//============================================================================

void Engine::DepthTexture2D::Create(DSVDescriptor* dsvDescriptor,
	SRVDescriptor* srvDescriptor, const DepthTextureCreateDesc& desc) {

	// 既にリソースが存在している場合は破棄する
	Destroy();

	dsvDescriptor_ = dsvDescriptor;
	srvDescriptor_ = srvDescriptor;

	// サイズ設定
	width_ = desc.width;
	height_ = desc.height;
	resourceFormat_ = desc.resourceFormat;
	dsvFormat_ = desc.dsvFormat;
	srvFormat_ = desc.srvFormat;

	// DSVを作成し、CPUハンドルを保存する
	dsvDescriptor->CreateDSV(desc.width, desc.height, dsvIndex_,
		dsvCPUHandle_, resource_, desc.resourceFormat, desc.dsvFormat);
	if (!desc.debugName.empty()) {

		resource_->SetName(desc.debugName.c_str());
	}

	// SRVの作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = desc.srvFormat;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDescriptor->CreateSRV(srvIndex_, resource_.Get(), srvDesc);
	srvGPUHandle_ = srvDescriptor->GetGPUHandle(srvIndex_);

	// 現在のリソース状態を深度書き込みに設定する
	currentState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

void Engine::DepthTexture2D::Destroy() {

	if (dsvDescriptor_) {

		dsvDescriptor_->Free(dsvIndex_);
	}
	if (srvDescriptor_) {

		srvDescriptor_->Free(srvIndex_);
	}
	resource_.Reset();
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
}

void Engine::DepthTexture2D::Transition(DxCommand& dxCommand, D3D12_RESOURCE_STATES newState) {

	// リソースが存在していない場合や、現在の状態と遷移先の状態が同じ場合は遷移しない
	if (!resource_ || currentState_ == newState) {
		return;
	}
	dxCommand.TransitionBarriers({ resource_.Get() }, currentState_, newState);
	currentState_ = newState;
}