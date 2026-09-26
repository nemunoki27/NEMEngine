#include "RenderingPlatform.h"

#include <stdexcept>

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>
#include <Engine/Core/Rendering/Shaders/ShaderCook.h>

// c++
#include <chrono>

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"dxguid.lib")

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
	const uint32_t frameContextCount =
		featureController_.GetPreferences().
		frameContextCount;
	GraphicsFrameState::SetActiveCount(
		frameContextCount);

	// command初期化、キュー/記録/フレーム終端の順で生成する
	dxCommandQueue_ = std::make_unique<DxCommandQueue>();
	dxCommandQueue_->Create(dxDevice_->Get());
	dxCommand_ = std::make_unique<DxCommand>();
	dxCommand_->Create(dxDevice_->Get());
	framePresenter_ = std::make_unique<FramePresenter>();
	framePresenter_->Create(dxDevice_->Get(), dxCommand_.get(), dxCommandQueue_.get());

	// 製品はCook済みDXILを使い、DXCをロードしない
	if (!ShaderCook::IsCookedProduct()) {
		dxShaderComplier_ = std::make_unique<DxShaderCompiler>();
		dxShaderComplier_->Init();
	}
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

void GraphicsPlatform::SubmitFrame() {

	framePresenter_->Submit();
}

void GraphicsPlatform::PresentFrame(IDXGISwapChain4* swapChain) {

	framePresenter_->Present(swapChain);
	resourceRetirement_.Seal(dxCommand_->GetFrameFenceValue(dxCommand_->GetCurrentFrameIndex()));
	resourceRetirement_.Collect(dxCommandQueue_->GetCompletedFenceValue());
}

void GraphicsPlatform::BeginFrame(uint32_t frameIndex) {

	frameIndex %= GraphicsFrameState::GetActiveCount();
	if (dxCommand_->IsRecording()) {
		Assert::Call(dxCommand_->GetCurrentFrameIndex() == frameIndex,
			"記録中のGraphicsFrameContextとSwapChain indexが一致しません");
	}

	const uint64_t fenceValue = dxCommand_->GetFrameFenceValue(frameIndex);
	const std::chrono::high_resolution_clock::time_point waitStart =
		std::chrono::high_resolution_clock::now();
	if (!dxCommandQueue_->WaitForFenceValue(fenceValue, "GraphicsPlatform::BeginFrame/FrameContextReuse")) {
		throw std::runtime_error("GPUの完了を確認できませんでした");
	}
	const std::chrono::duration<float, std::milli> waitElapsed =
		std::chrono::high_resolution_clock::now() - waitStart;
	FrameProfiler::GetInstance().AddSample(
		FrameProfiler::Category::GPUWait, waitElapsed.count());

	dxCommand_->BeginFrame(frameIndex);
	const uint64_t completedFenceValue =
		dxCommandQueue_->GetCompletedFenceValue();
	resourceRetirement_.Collect(completedFenceValue);
	const uint64_t lastFenceValue =
		dxCommandQueue_->GetLastSignaledFenceValue();
	FrameProfiler::GetInstance().SetFrameContextStatistics(
		frameIndex, GraphicsFrameState::GetActiveCount(),
		static_cast<uint32_t>(lastFenceValue - completedFenceValue));
}

void GraphicsPlatform::WaitForGPU() {

	if (IsDeviceRemoved()) {
		throw std::runtime_error("Deviceが失われたため描画を継続できません");
	}

	// 初期化途中でQueueを作れなかった場合は提出対象がない
	if (!dxCommandQueue_ || !dxCommandQueue_->IsInitialized()) {
		return;
	}
	// 現在積んでいるリストを実行してGPU完了まで待つ、終了時のドレイン用
	if (dxCommand_ && dxCommand_->IsRecording()) {
		dxCommand_->CloseCommandList();
		dxCommandQueue_->ExecuteCommandList(dxCommand_->GetCommandList());
		const uint64_t fenceValue = dxCommandQueue_->Signal();
		if (!dxCommandQueue_->WaitForFenceValue(fenceValue, "GraphicsPlatform::WaitForGPU/Drain")) {
			throw std::runtime_error("GPUの完了を確認できませんでした");
		}
		resourceRetirement_.Seal(fenceValue);
		resourceRetirement_.Collect(dxCommandQueue_->GetCompletedFenceValue());
		dxCommand_->SetCurrentFrameFenceValue(fenceValue);
		dxCommand_->ResetCommandList();
		return;
	}

	const uint64_t fenceValue = dxCommandQueue_->Signal();
	if (!dxCommandQueue_->WaitForFenceValue(fenceValue, "GraphicsPlatform::WaitForGPU/QueueDrain")) {
		throw std::runtime_error("GPUの完了を確認できませんでした");
	}
	resourceRetirement_.Seal(fenceValue);
	resourceRetirement_.Collect(dxCommandQueue_->GetCompletedFenceValue());
}

bool Engine::GraphicsPlatform::IsDeviceRemoved() const {

	return dxDevice_ && dxDevice_->Get() && FAILED(dxDevice_->Get()->GetDeviceRemovedReason());
}
