#include "RenderTexture2D.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <stdexcept>

//============================================================================
//	RenderTexture2D classMethods
//============================================================================
Engine::RenderTexture2D::~RenderTexture2D() {

	Destroy();
}

void Engine::RenderTexture2D::Create(ID3D12Device* device, RTVDescriptor* rtvDescriptor,
	SRVDescriptor* srvDescriptor, const RenderTextureCreateDesc& desc) {

	if (!device || !rtvDescriptor || !srvDescriptor || desc.width == 0 || desc.height == 0) {
		throw std::invalid_argument("RenderTexture2Dの作成条件が不正です");
	}
	RenderTexture2D candidate;

	candidate.rtvDescriptor_ = rtvDescriptor;
	candidate.srvDescriptor_ = srvDescriptor;

	// 描画レンダーテクスチャの情報を保存する
	candidate.format_ = desc.format;
	candidate.hasUAV_ = desc.createUAV;
	candidate.renderTarget_.width = desc.width;
	candidate.renderTarget_.height = desc.height;
	candidate.renderTarget_.format = desc.format;
	candidate.renderTarget_.clearColor = desc.clearColor;

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
	clearValue.Color[0] = candidate.renderTarget_.clearColor.r;
	clearValue.Color[1] = candidate.renderTarget_.clearColor.g;
	clearValue.Color[2] = candidate.renderTarget_.clearColor.b;
	clearValue.Color[3] = candidate.renderTarget_.clearColor.a;

	// リソースの生成
	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
		&resourceDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&candidate.resource_));
	if (!DxDredDiagnostics::CheckHRESULT(device, hr, "RenderTexture2D::Create")) {
		throw std::runtime_error("RenderTexture2D用リソースの作成に失敗しました");
	}
	if (!desc.debugName.empty()) {

		candidate.resource_->SetName(desc.debugName.c_str());
	}

	// RTVの生成
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = desc.format;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDescriptor->Create(candidate.rtvIndex_, candidate.renderTarget_.rtvHandle, candidate.resource_.Get(), rtvDesc);

	// SRVの生成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = desc.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDescriptor->CreateSRV(candidate.srvIndex_, candidate.resource_.Get(), srvDesc);
	candidate.srvGPUHandle_ = srvDescriptor->GetGPUHandle(candidate.srvIndex_);

	// UAVの生成
	if (desc.createUAV) {

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = desc.format;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = 0;
		srvDescriptor->CreateUAV(candidate.uavIndex_, candidate.resource_.Get(), uavDesc);
		candidate.uavGPUHandle_ = srvDescriptor->GetGPUHandle(candidate.uavIndex_);
	}

	Logger::Output(LogType::Engine, "RenderTexture2Dを作成しました: {}x{}", desc.width, desc.height);
	Logger::Output(LogType::Engine, "名前: {}", Algorithm::ConvertString(desc.debugName));
	Logger::Output(LogType::Engine, "形式: {}", std::string(EnumAdapter<DXGI_FORMAT>::ToString(desc.format)));
	Logger::Output(LogType::Engine, "UAVを使用: {}", desc.createUAV ? "はい" : "いいえ");

	// 現在のリソース状態をレンダーターゲットに設定する
	candidate.currentState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;

	// 作成途中の失敗では旧描画先を維持する
	Swap(candidate);
}

void Engine::RenderTexture2D::Destroy() {

	if (rtvDescriptor_ && rtvIndex_ != UINT32_MAX) {

		rtvDescriptor_->Retire(rtvIndex_, {});
	}
	if (srvDescriptor_) {

		if (srvIndex_ != UINT32_MAX) {

			srvDescriptor_->Retire(srvIndex_, {});
		}
		if (uavIndex_ != UINT32_MAX) {

			srvDescriptor_->Retire(uavIndex_, {});
		}
	}

	if (resource_) {
		srvDescriptor_->GetRetirementQueue().Retire(std::move(resource_));
	}
	renderTarget_ = RenderTarget{};
	srvGPUHandle_ = {};
	uavGPUHandle_ = {};
	currentState_ = D3D12_RESOURCE_STATE_COMMON;
	format_ = DXGI_FORMAT_UNKNOWN;
	hasUAV_ = false;
	rtvIndex_ = UINT32_MAX;
	srvIndex_ = UINT32_MAX;
	uavIndex_ = UINT32_MAX;
	rtvDescriptor_ = nullptr;
	srvDescriptor_ = nullptr;
}

void Engine::RenderTexture2D::Transition(DxCommand& dxCommand, D3D12_RESOURCE_STATES newState) {

	// すでにリソースが存在していない場合や、現在の状態と遷移先の状態が同じ場合は遷移しない
	if (!resource_ || currentState_ == newState) {
		return;
	}
	dxCommand.TransitionBarriers({ resource_.Get() }, currentState_, newState);
	currentState_ = newState;
}

void Engine::RenderTexture2D::Swap(RenderTexture2D& other) noexcept {

	std::swap(rtvDescriptor_, other.rtvDescriptor_);
	std::swap(srvDescriptor_, other.srvDescriptor_);
	std::swap(renderTarget_, other.renderTarget_);
	std::swap(resource_, other.resource_);
	std::swap(srvGPUHandle_, other.srvGPUHandle_);
	std::swap(uavGPUHandle_, other.uavGPUHandle_);
	std::swap(currentState_, other.currentState_);
	std::swap(format_, other.format_);
	std::swap(hasUAV_, other.hasUAV_);
	std::swap(rtvIndex_, other.rtvIndex_);
	std::swap(srvIndex_, other.srvIndex_);
	std::swap(uavIndex_, other.uavIndex_);
}
