#include "RenderingCore.h"

// engine
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	GraphicsCore classMethods
//============================================================================
void Engine::GraphicsCore::Init(bool usesEditorUI) {

	// 各コアの初期化
	engineContext_ = std::make_unique<EngineContext>();
	engineContext_->Init(usesEditorUI);
	graphicsPlatform_ = std::make_unique<GraphicsPlatform>();
	graphicsPlatform_->Init();

	const auto& window = engineContext_->GetWindowSetting();
	const auto& graphics = engineContext_->GetGraphicsSetting();
	ID3D12Device8* device = graphicsPlatform_->GetDevice();
	const Vector2I clientSize = WinApp::GetClientSize();
	const uint32_t frameWidth = clientSize.x > 0 ?
		static_cast<uint32_t>(clientSize.x) : static_cast<uint32_t>(window.engineSize.x);
	const uint32_t frameHeight = clientSize.y > 0 ?
		static_cast<uint32_t>(clientSize.y) : static_cast<uint32_t>(window.engineSize.y);

	// デスクリプタ初期化
	rtvDescriptor_ = std::make_unique<RTVDescriptor>();
	rtvDescriptor_->Init(device, DescriptorType(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE));
	dsvDescriptor_ = std::make_unique<DSVDescriptor>();
	dsvDescriptor_->Init(device, DescriptorType(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE));
	srvDescriptor_ = std::make_unique<SRVDescriptor>();
	srvDescriptor_->Init(device, DescriptorType(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));
	auto& retirement = graphicsPlatform_->GetResourceRetirement();
	rtvDescriptor_->SetRetirementQueue(retirement);
	dsvDescriptor_->SetRetirementQueue(retirement);
	srvDescriptor_->SetRetirementQueue(retirement);
	// フレームバッファ用のDSVを初期化
	dsvDescriptor_->InitFrameBufferDSV(frameWidth, frameHeight);

	// スワップチェーン初期化
	swapChain_ = std::make_unique<DxSwapChain>();
	DisplayOutputSettings displayOutput = graphicsPlatform_->GetFeatureController()
		.GetPreferences().displayOutput;
	if (usesEditorUI) {
		// ImGuiのPlatform Windowを含むエディター出力はSDRへ統一する
		displayOutput.mode = DisplayOutputMode::SDR;
	}
	swapChain_->Create(engineContext_->GetWinApp(), device, graphicsPlatform_->GetDxgiFactory(), graphicsPlatform_->GetCommandQueue()->GetQueue(),
		rtvDescriptor_.get(), frameWidth, frameHeight, graphics.swapChainFormat,
		graphics.clearColor, displayOutput);

	// 静的GPUバッファ転送サービスの初期化(テクスチャ用とは独立)
	bufferUploadService_ = std::make_unique<BufferUploadService>();
	bufferUploadService_->Init(graphicsPlatform_->GetResourceRetirement(), device, graphicsPlatform_->GetCommandQueue()->GetQueue());

	// テクスチャ関連の初期化
	textureUploadService_ = std::make_unique<TextureUploadService>();
	textureUploadService_->Init(device, srvDescriptor_.get());
	builtinTextureLibrary_ = std::make_unique<BuiltinTextureLibrary>();
	builtinTextureLibrary_->Init(*textureUploadService_);
}

void Engine::GraphicsCore::TickFrameServices() {

	textureUploadService_->TickFinalize();
	bufferUploadService_->TickFinalize();
}

void Engine::GraphicsCore::SyncWindowSize() {

	const Vector2I clientSize = WinApp::GetClientSize();
	if (clientSize.x <= 0 || clientSize.y <= 0) {
		return;
	}

	const uint32_t width = static_cast<uint32_t>(clientSize.x);
	const uint32_t height = static_cast<uint32_t>(clientSize.y);
	if (swapChain_->GetDesc().Width == width && swapChain_->GetDesc().Height == height) {
		return;
	}

	const uint32_t previousWidth = swapChain_->GetDesc().Width;
	const uint32_t previousHeight = swapChain_->GetDesc().Height;
	graphicsPlatform_->WaitForGPU();
	if (swapChain_->Resize(width, height)) {
		dsvDescriptor_->ResizeFrameBufferDSV(width, height);
		Logger::Output(LogType::Engine, "SwapChainをリサイズしました: {}x{} -> {}x{}",
			previousWidth, previousHeight, width, height);
	}
}

void Engine::GraphicsCore::BeginRenderFrame() {

	graphicsPlatform_->BeginFrame(swapChain_->GetCurrentBackBufferIndex());
	auto* dxCommand = graphicsPlatform_->GetDxCommand();

	// Present -> RenderTarget
	dxCommand->TransitionBarriers({ swapChain_->GetCurrentResource() },
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void Engine::GraphicsCore::Render() {

	auto* dxCommand = graphicsPlatform_->GetDxCommand();
	const DXGI_SWAP_CHAIN_DESC1& swapChainDesc = swapChain_->GetDesc();

	// 描画に必要な情報設定
	dxCommand->SetRenderTargets(std::optional<RenderTarget>(swapChain_->GetRenderTarget()), dsvDescriptor_->GetFrameCPUHandle());
	dxCommand->ClearDepthStencilView(dsvDescriptor_->GetFrameCPUHandle());
	dxCommand->SetViewportAndScissor(swapChainDesc.Width, swapChainDesc.Height);
}
void Engine::GraphicsCore::SubmitRenderFrame() {

	auto* dxCommand = graphicsPlatform_->GetDxCommand();

	// 描画中に集めたDEFAULT heap差分を1Batchで提出し
	// 後続の描画QueueだけをGPU側で待たせる
	bufferUploadService_->SubmitBatch();

	// RenderTarget -> Present
	dxCommand->TransitionBarriers({ swapChain_->GetCurrentResource() },
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	// 外部ウィンドウ描画より先にメイン描画を提出する
	graphicsPlatform_->SubmitFrame();
}

void Engine::GraphicsCore::EndRenderFrame() {

	// 全描画提出後にメインウィンドウをPresentする
	graphicsPlatform_->PresentFrame(swapChain_->Get());
}

void Engine::GraphicsCore::Finalize() {

	// Device消失後は待機せず、所有元の終了を続ける
	auto drain = [&] {
		if (graphicsPlatform_ && graphicsPlatform_->IsDeviceRemoved()) return;
		try {
			if (bufferUploadService_) bufferUploadService_->FlushAndWait();
			if (graphicsPlatform_) graphicsPlatform_->WaitForGPU();
		} catch (...) {
			if (!graphicsPlatform_ || !graphicsPlatform_->IsDeviceRemoved()) throw;
		}
	};
	drain();

	// Device/Queue/Descriptorを参照するサービスはGraphicsPlatformより先に解放する
	if (builtinTextureLibrary_) {
		builtinTextureLibrary_->Finalize();
	}
	if (textureUploadService_) {
		textureUploadService_->Finalize();
	}
	if (bufferUploadService_) {
		// GPU使用中のstagingを巻き込まないよう、未完了分を待ってから解放する
		bufferUploadService_->Finalize();
	}
	builtinTextureLibrary_.reset();
	textureUploadService_.reset();
	bufferUploadService_.reset();

	// サービス終了中の退避もDescriptor破棄前に回収する
	drain();
	if (graphicsPlatform_ && graphicsPlatform_->IsDeviceRemoved()) {
		graphicsPlatform_->GetResourceRetirement().ReleaseAfterDeviceRemoval(graphicsPlatform_->GetDevice());
	}

	// 描画リソースとDescriptor heapをDevice破棄前に解放する
	swapChain_.reset();
	srvDescriptor_.reset();
	dsvDescriptor_.reset();
	rtvDescriptor_.reset();

	if (graphicsPlatform_) {
		graphicsPlatform_->Finalize(WinApp::GetHwnd());
	}
	if (engineContext_) {
		engineContext_->Finalize();
	}
	graphicsPlatform_.reset();
	engineContext_.reset();
}
