#include "RenderTexture2D.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>
#include <Engine/Logger/Logger.h>
#include <Engine/Core/Graphics/Descriptors/RTVDescriptor.h>
#include <Engine/Core/Graphics/Descriptors/SRVDescriptor.h>
#include <Engine/Core/Graphics/DxObject/DxCommand.h>
#include <Engine/Utility/Enum/EnumAdapter.h>
#include <Engine/Utility/Algorithm/Algorithm.h>

//============================================================================
//	RenderTexture2D classMethods
//============================================================================

void Engine::RenderTexture2D::Create(ID3D12Device* device, RTVDescriptor* rtvDescriptor,
	SRVDescriptor* srvDescriptor, const RenderTextureCreateDesc& desc) {

	// 既にリソースが存在している場合は破棄する
	Destroy();

	rtvDescriptor_ = rtvDescriptor;
	srvDescriptor_ = srvDescriptor;

	// 描画レンダーテクスチャの情報を保存する
	format_ = desc.format;
	hasUAV_ = desc.createUAV;
	renderTarget_.width = desc.width;
	renderTarget_.height = desc.height;
	renderTarget_.clearColor = desc.clearColor;

	// リソースデスクリプションの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = desc.width;
	resourceDesc.Height = desc.height;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = desc.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	// UAVを作成する場合は、レンダーターゲットとUAVの両方を許可するフラグを設定する
	resourceDesc.Flags = desc.createUAV ? static_cast<D3D12_RESOURCE_FLAGS>(
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) :
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// クリア値の設定
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = desc.format;
	clearValue.Color[0] = renderTarget_.clearColor.r;
	clearValue.Color[1] = renderTarget_.clearColor.g;
	clearValue.Color[2] = renderTarget_.clearColor.b;
	clearValue.Color[3] = renderTarget_.clearColor.a;

	// リソースの生成
	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
		&resourceDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&resource_));
	assert(SUCCEEDED(hr));
	if (!desc.debugName.empty()) {

		resource_->SetName(desc.debugName.c_str());
	}

	// RTVの生成
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = desc.format;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDescriptor->Create(rtvIndex_, renderTarget_.rtvHandle, resource_.Get(), rtvDesc);

	// SRVの生成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = desc.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDescriptor->CreateSRV(srvIndex_, resource_.Get(), srvDesc);
	srvGPUHandle_ = srvDescriptor->GetGPUHandle(srvIndex_);

	// UAVの生成
	if (desc.createUAV) {

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = desc.format;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		srvDescriptor->CreateUAV(uavIndex_, resource_.Get(), uavDesc);
		uavGPUHandle_ = srvDescriptor->GetGPUHandle(uavIndex_);
	}

	Logger::Output(LogType::Engine, "Created RenderTexture2D: {}x{}", desc.width, desc.height);
	Logger::Output(LogType::Engine, "Name: {}", Algorithm::ConvertString(desc.debugName));
	Logger::Output(LogType::Engine, "Format: {}", std::string(EnumAdapter<DXGI_FORMAT>::ToString(desc.format)));
	Logger::Output(LogType::Engine, "UseUAV: {}", desc.createUAV ? "Yes" : "No");

	// 現在のリソース状態をレンダーターゲットに設定する
	currentState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
}

void Engine::RenderTexture2D::Destroy() {

	if (rtvDescriptor_) {

		rtvDescriptor_->Free(rtvIndex_);
	}
	if (srvDescriptor_) {

		srvDescriptor_->Free(srvIndex_);
		if (uavIndex_ != UINT32_MAX) {

			srvDescriptor_->Free(uavIndex_);
		}
	}

	resource_.Reset();
	renderTarget_ = RenderTarget{};
	srvGPUHandle_ = {};
	uavGPUHandle_ = {};
	currentState_ = D3D12_RESOURCE_STATE_COMMON;
	format_ = DXGI_FORMAT_UNKNOWN;
	hasUAV_ = false;
	rtvIndex_ = UINT32_MAX;
	srvIndex_ = UINT32_MAX;
	uavIndex_ = UINT32_MAX;
}

void Engine::RenderTexture2D::Transition(DxCommand& dxCommand, D3D12_RESOURCE_STATES newState) {

	// すでにリソースが存在していない場合や、現在の状態と遷移先の状態が同じ場合は遷移しない
	if (!resource_ || currentState_ == newState) {
		return;
	}
	dxCommand.TransitionBarriers({ resource_.Get() }, currentState_, newState);
	currentState_ = newState;
}