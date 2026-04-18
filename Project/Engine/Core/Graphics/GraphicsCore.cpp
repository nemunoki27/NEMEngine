#include "GraphicsCore.h"

//============================================================================
//	GraphicsCore classMethods
//============================================================================

void Engine::GraphicsCore::Init() {

	// 各コアの初期化
	engineContext_ = std::make_unique<EngineContext>();
	engineContext_->Init();
	graphicsPlatform_ = std::make_unique<GraphicsPlatform>();
	graphicsPlatform_->Init();

	const auto& window = engineContext_->GetWindowSetting();
	const auto& graphics = engineContext_->GetGraphicsSetting();
	ID3D12Device8* device = graphicsPlatform_->GetDevice();

	// デスクリプタ初期化
	rtvDescriptor_ = std::make_unique<RTVDescriptor>();
	rtvDescriptor_->Init(device, DescriptorType(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE));
	dsvDescriptor_ = std::make_unique<DSVDescriptor>();
	dsvDescriptor_->Init(device, DescriptorType(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE));
	srvDescriptor_ = std::make_unique<SRVDescriptor>();
	srvDescriptor_->Init(device, DescriptorType(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE));
	// フレームバッファ用のDSVを初期化
	dsvDescriptor_->InitFrameBufferDSV(window.engineSize.x, window.engineSize.y);

	// スワップチェーン初期化
	swapChain_ = std::make_unique<DxSwapChain>();
	swapChain_->Create(engineContext_->GetWinApp(), graphicsPlatform_->GetDxgiFactory(), graphicsPlatform_->GetDxCommand()->GetQueue(),
		rtvDescriptor_.get(), window.engineSize.x, window.engineSize.y, graphics.swapChainFormat, graphics.clearColor);

	// テクスチャ関連の初期化
	textureUploadService_ = std::make_unique<TextureUploadService>();
	textureUploadService_->Init(device, srvDescriptor_.get());
	builtinTextureLibrary_ = std::make_unique<BuiltinTextureLibrary>();
	builtinTextureLibrary_->Init(*textureUploadService_);
}

void Engine::GraphicsCore::TickFrameServices() {

	textureUploadService_->TickFinalize();
}

void Engine::GraphicsCore::BeginRenderFrame() {

	auto* dxCommand = graphicsPlatform_->GetDxCommand();

	// Present -> RenderTarget
	dxCommand->TransitionBarriers({ swapChain_->GetCurrentResource() },
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void Engine::GraphicsCore::Render() {

	auto* dxCommand = graphicsPlatform_->GetDxCommand();
	const auto& window = engineContext_->GetWindowSetting();

	// 描画に必要な情報設定
	dxCommand->SetRenderTargets(std::optional<RenderTarget>(swapChain_->GetRenderTarget()), dsvDescriptor_->GetFrameCPUHandle());
	dxCommand->ClearDepthStencilView(dsvDescriptor_->GetFrameCPUHandle());
	dxCommand->SetViewportAndScissor(window.engineSize.x, window.engineSize.y);
}

void Engine::GraphicsCore::EndRenderFrame() {

	auto* dxCommand = graphicsPlatform_->GetDxCommand();

	// RenderTarget -> Present
	dxCommand->TransitionBarriers({ swapChain_->GetCurrentResource() },
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	// コマンド実行
	dxCommand->ExecuteCommands(swapChain_->Get());
}

void Engine::GraphicsCore::Finalize() {

	// GPUが完了するまで待機
	graphicsPlatform_->GetDxCommand()->WaitForGPU();

	graphicsPlatform_->Finalize(WinApp::GetHwnd());
	engineContext_->Finalize();
	builtinTextureLibrary_->Finalize();
	textureUploadService_->Finalize();

	swapChain_.reset();
	srvDescriptor_.reset();
	dsvDescriptor_.reset();
	rtvDescriptor_.reset();
	graphicsPlatform_.reset();
	engineContext_.reset();
	builtinTextureLibrary_.reset();
	textureUploadService_.reset();
}