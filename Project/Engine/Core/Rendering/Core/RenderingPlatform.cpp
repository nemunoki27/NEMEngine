#include "RenderingPlatform.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

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
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	DxDredDiagnostics::EnableBeforeDeviceCreation();
#endif

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
	DxDredDiagnostics::ResetForNewDevice();

	ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if (SUCCEEDED(dxDevice_->Get()->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {

		// API の破損や不正引数は即座に止める
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);

		// Device Removed 時に DRED Dump へ到達させるため、
		// ERROR 全般の即時 break は一時的に無効化する
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);

		// 明示的に Device Removal 系の break も解除する
		infoQueue->SetBreakOnID(D3D12_MESSAGE_ID_DEVICE_REMOVAL_PROCESS_AT_FAULT, FALSE);
		infoQueue->SetBreakOnID(D3D12_MESSAGE_ID_DEVICE_REMOVAL_PROCESS_POSSIBLY_AT_FAULT, FALSE);
		infoQueue->SetBreakOnID(D3D12_MESSAGE_ID_DEVICE_REMOVAL_PROCESS_NOT_AT_FAULT, FALSE);
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

	// DxCommandはDeviceを参照しているため、Deviceより先にFinalize/resetする
	if (dxCommand_) {
		dxCommand_->Finalize(hwnd);
		dxCommand_.reset();
	}
	dxShaderComplier_.reset();
	dxDevice_.reset();
}
