#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Core/Graphics/Render/Texture/MultiRenderTarget.h>

// c++
#include <memory>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	ViewportRenderService class
	//	ビューポートの描画に必要なリソースを管理するクラス
	//============================================================================
	class ViewportRenderService {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ViewportRenderService() = default;
		~ViewportRenderService() = default;

		// 描画ビューのサーフェスを生成する
		void SyncSurface(GraphicsCore& graphicsCore, RenderViewKind kind, uint32_t width, uint32_t height);

		//--------- accessor -----------------------------------------------------

		// 描画ビューのサーフェスの取得
		MultiRenderTarget* GetSurface(RenderViewKind kind);
		const MultiRenderTarget* GetSurface(RenderViewKind kind) const;

		// 描画ビューのサーフェスの色レンダーテクスチャの取得
		RenderTexture2D* GetDisplayTexture(RenderViewKind kind, size_t colorIndex = 0);
		const RenderTexture2D* GetDisplayTexture(RenderViewKind kind, size_t colorIndex = 0) const;

		// 描画ビューのエイリアスの取得
		static const char* GetViewAlias(RenderViewKind kind);
		//描画ビューの名前取得
		static const char* GetPrimaryColorName(RenderViewKind kind);
		static const char* GetPrimaryDepthName(RenderViewKind kind);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 描画ビューのスロット
		struct SurfaceSlot {

			uint32_t width = 0;
			uint32_t height = 0;
			std::unique_ptr<MultiRenderTarget> surface;
		};

		//--------- variables ----------------------------------------------------

		// 各ビューのスロット
		SurfaceSlot game_;
		SurfaceSlot scene_;

		//--------- functions ----------------------------------------------------

		SurfaceSlot& GetSlot(RenderViewKind kind);
		const SurfaceSlot& GetSlot(RenderViewKind kind) const;

		// 描画ビューのサーフェスの情報を構築する
		static MultiRenderTargetCreateDesc BuildDefaultDesc(RenderViewKind kind, uint32_t width, uint32_t height);
	};
} // Engine