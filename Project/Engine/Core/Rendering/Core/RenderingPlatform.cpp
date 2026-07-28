#include "RenderingPlatform.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

// c++
#include <chrono>

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

		// APIの破損や不正引数は即座に止める
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);

		// Device Removed時にDRED Dumpへ到達させるため、
		// ERROR全般の即時breakは一時的に無効化する
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);

		// 明示的にDevice Removal系のbreakも解除する
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

	// command初期化、キュー/記録/フレーム終端の順で生成する
	dxCommandQueue_ = std::make_unique<DxCommandQueue>();
	dxCommandQueue_->Create(dxDevice_->Get());
	dxCommand_ = std::make_unique<DxCommand>();
	dxCommand_->Create(dxDevice_->Get());
	framePresenter_ = std::make_unique<FramePresenter>();
	framePresenter_->Create(dxDevice_->Get(), dxCommand_.get(), dxCommandQueue_.get());

	// DXC初期化
	dxShaderComplier_ = std::make_unique<DxShaderCompiler>();
	dxShaderComplier_->Init();
}

void GraphicsPlatform::Finalize(HWND hwnd) {

	// Queue/Presenterはフェンスやタイマーを抱えるためDeviceより先に破棄する
	if (framePresenter_) {
		framePresenter_->Finalize();
		framePresenter_.reset();
	}
	dxCommand_.reset();
	if (dxCommandQueue_) {
		dxCommandQueue_->Finalize();
		dxCommandQueue_.reset();
	}
	dxShaderComplier_.reset();
	dxDevice_.reset();

	// ウィンドウを閉じる
	CloseWindow(hwnd);
}

void GraphicsPlatform::PresentFrame(IDXGISwapChain4* swapChain) {

	framePresenter_->Present(swapChain);
}

void GraphicsPlatform::BeginFrame(uint32_t frameIndex) {

	frameIndex %= kGraphicsFrameContextCount;
	if (dxCommand_->IsRecording()) {
		Assert::Call(dxCommand_->GetCurrentFrameIndex() == frameIndex,
			"記録中のGraphicsFrameContextとSwapChain indexが一致しません");
	}

	const uint64_t fenceValue = dxCommand_->GetCurrentFrameFenceValue();
	const std::chrono::high_resolution_clock::time_point waitStart =
		std::chrono::high_resolution_clock::now();
	dxCommandQueue_->WaitForFenceValue(
		fenceValue, "GraphicsPlatform::BeginFrame/FrameContextReuse");
	const std::chrono::duration<float, std::milli> waitElapsed =
		std::chrono::high_resolution_clock::now() - waitStart;
	FrameProfiler::GetInstance().AddSample(
		FrameProfiler::Category::GPUWait, waitElapsed.count());

	dxCommand_->BeginFrame(frameIndex);
	const uint64_t completedFenceValue =
		dxCommandQueue_->GetCompletedFenceValue();
	const uint64_t lastFenceValue =
		dxCommandQueue_->GetLastSignaledFenceValue();
	FrameProfiler::GetInstance().SetFrameContextStatistics(
		frameIndex, kGraphicsFrameContextCount,
		static_cast<uint32_t>(lastFenceValue - completedFenceValue));
}

void GraphicsPlatform::WaitForGPU() {

	// 現在積んでいるリストを実行してGPU完了まで待つ、終了時のドレイン用
	if (dxCommand_->IsRecording()) {
		dxCommand_->CloseCommandList();
		dxCommandQueue_->ExecuteCommandList(dxCommand_->GetCommandList());
		const uint64_t fenceValue = dxCommandQueue_->Signal();
		dxCommandQueue_->WaitForFenceValue(
			fenceValue, "GraphicsPlatform::WaitForGPU/Drain");
		dxCommand_->SetCurrentFrameFenceValue(fenceValue);
		dxCommand_->ResetCommandList();
		return;
	}

	const uint64_t fenceValue = dxCommandQueue_->Signal();
	dxCommandQueue_->WaitForFenceValue(
		fenceValue, "GraphicsPlatform::WaitForGPU/QueueDrain");
}
