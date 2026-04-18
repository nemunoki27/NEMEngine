#include "GraphicsPlatform.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Debug/Assert.h>

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"dxcompiler.lib")

//============================================================================
//	GraphicsPlatform classMethods
//============================================================================

namespace {
#if defined(D3D_SHADER_MODEL_6_8)
	constexpr D3D_SHADER_MODEL kRequestedHighestShaderModel = D3D_SHADER_MODEL_6_8;
#elif defined(D3D_SHADER_MODEL_6_7)
	constexpr D3D_SHADER_MODEL kRequestedHighestShaderModel = D3D_SHADER_MODEL_6_7;
#else
	constexpr D3D_SHADER_MODEL kRequestedHighestShaderModel = D3D_SHADER_MODEL_6_6;
#endif
}

void GraphicsPlatform::InitDXDevice() {
#ifdef _DEBUG
	ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {

		// デバッグレイヤーを有効化する
		debugController->EnableDebugLayer();

		// さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	dxDevice_->Create();

	ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if (SUCCEEDED(dxDevice_->Get()->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {

		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		D3D12_MESSAGE_ID denyIDs[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
			D3D12_MESSAGE_ID_FENCE_ZERO_WAIT
		};

		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIDs);
		filter.DenyList.pIDList = denyIDs;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		infoQueue->PushStorageFilter(&filter);
	}
	DetectFeatureSupport();
}

D3D_SHADER_MODEL GraphicsPlatform::QueryHighestShaderModel() const {

	D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{};
	shaderModel.HighestShaderModel = kRequestedHighestShaderModel;

	const HRESULT hr = dxDevice_->Get()->CheckFeatureSupport(
		D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
	if (FAILED(hr)) {
		return D3D_SHADER_MODEL_5_1;
	}
	return shaderModel.HighestShaderModel;
}

void GraphicsPlatform::DetectFeatureSupport() {

	GraphicsAdapterInfo adapterInfo{};
	adapterInfo.adapterName = dxDevice_->GetAdapterName();
	adapterInfo.featureLevel = dxDevice_->GetFeatureLevel();
	adapterInfo.dedicatedVideoMemoryBytes = dxDevice_->GetDedicatedVideoMemoryBytes();

	GraphicsFeatureSupport support{};
	support.highestShaderModel = QueryHighestShaderModel();

	D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
	if (SUCCEEDED(dxDevice_->Get()->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1)))) {

		support.waveOps = (options1.WaveOps == TRUE);
	}

	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
	if (SUCCEEDED(dxDevice_->Get()->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {

		support.raytracingTier = options5.RaytracingTier;
	}

	D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
	if (SUCCEEDED(dxDevice_->Get()->CheckFeatureSupport(
		D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)))) {

		support.meshShaderTier = options7.MeshShaderTier;
	}

	featureController_.ApplyDetectedSupport(adapterInfo, support);
}

void GraphicsPlatform::Init() {

	// device初期化
	dxDevice_ = std::make_unique<DxDevice>();
	InitDXDevice();

	// command初期化
	dxCommand_ = std::make_unique<DxCommand>();
	dxCommand_->Create(dxDevice_->Get());

	// DXC初期化
	dxShaderComplier_ = std::make_unique<DxShaderCompiler>();
	dxShaderComplier_->Init();
}

void GraphicsPlatform::Finalize(HWND hwnd) {

	dxCommand_->Finalize(hwnd);

	dxDevice_.reset();
	dxCommand_.reset();
	dxShaderComplier_.reset();
}
