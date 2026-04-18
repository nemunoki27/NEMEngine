#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Context/EngineContext.h>
#include <Engine/Core/Graphics/GraphicsPlatform.h>
#include <Engine/Core/Graphics/DxObject/DxSwapChain.h>
#include <Engine/Core/Graphics/Descriptors/RTVDescriptor.h>
#include <Engine/Core/Graphics/Descriptors/DSVDescriptor.h>
#include <Engine/Core/Graphics/Descriptors/SRVDescriptor.h>
#include <Engine/Core/Graphics/Texture/TextureUploadService.h>
#include <Engine/Core/Graphics/Texture/BuiltinTextureLibrary.h>

namespace Engine {

	//============================================================================
	//	GraphicsCore class
	//	グラフィックス全般の管理を行うクラス
	//============================================================================
	class GraphicsCore {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		GraphicsCore() = default;
		~GraphicsCore() = default;

		// 初期化
		void Init();

		// 毎フレーム更新
		void TickFrameServices();

		// フレーム開始/終了処理
		void BeginRenderFrame();
		void EndRenderFrame();

		// 描画
		void Render();

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		// 各コアのアクセサ
		EngineContext& GetContext() { return *engineContext_; }
		GraphicsPlatform& GetDXObject() { return *graphicsPlatform_; }

		// デスクリプタ管理クラスのアクセサ
		RTVDescriptor& GetRTVDescriptor() { return *rtvDescriptor_; }
		DSVDescriptor& GetDSVDescriptor() { return *dsvDescriptor_; }
		SRVDescriptor& GetSRVDescriptor() { return *srvDescriptor_; }

		// テクスチャ関連のアクセサ
		TextureUploadService& GetTextureUploadService() { return *textureUploadService_; }
		BuiltinTextureLibrary& GetBuiltinTextureLibrary() { return *builtinTextureLibrary_; }

		// 描画情報
		// バックバッファの情報取得
		const RenderTarget& GetBackBufferRenderTarget() const { return swapChain_->GetRenderTarget(); }
		ID3D12Resource* GetBackBufferResource() const { return swapChain_->GetCurrentResource(); }
		const DXGI_SWAP_CHAIN_DESC1& GetSwapChainDesc() const { return swapChain_->GetDesc(); }

		// フレームバッファのDSVを取得
		const D3D12_CPU_DESCRIPTOR_HANDLE& GetFrameDepthHandle() const { return dsvDescriptor_->GetFrameCPUHandle(); }

		// ウインドウメッセージの処理
		bool GetProcessMessage() const { return engineContext_->GetWinApp()->ProcessMessage(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// エンジン全体のコンテキスト
		std::unique_ptr<EngineContext> engineContext_;
		// DXオブジェクトを提供
		std::unique_ptr<GraphicsPlatform> graphicsPlatform_;

		// デスクリプタ管理クラス
		std::unique_ptr<RTVDescriptor> rtvDescriptor_;
		std::unique_ptr<DSVDescriptor> dsvDescriptor_;
		std::unique_ptr<SRVDescriptor> srvDescriptor_;

		// スワップチェーン
		std::unique_ptr<DxSwapChain> swapChain_;

		// GPUテクスチャ
		std::unique_ptr<TextureUploadService> textureUploadService_;
		std::unique_ptr<BuiltinTextureLibrary> builtinTextureLibrary_;
	};
} // Engine