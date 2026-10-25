#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Core/RenderingFeatureTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>

// c++
#include <memory>
#include <span>
#include <unordered_map>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	ViewportRenderService class
	//	ビューポートの描画に必要なリソースを管理するクラス
	//============================================================================
	class ViewportRenderService {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ViewportRenderService() = default;
		~ViewportRenderService();

		// 終了処理
		void Finalize();

		// 描画ビューのサーフェスを生成する
		void SyncSurface(GraphicsCore& graphicsCore, RenderViewKind kind, uint32_t width, uint32_t height);
		// CameraのRenderTexture出力先を生成する
		void SyncRenderTextureSurface(GraphicsCore& graphicsCore, AssetID assetID,
			uint32_t width, uint32_t height);
		void SyncRenderTextureSurface(const RenderTargetCreationContext& context, AssetID assetID,
			uint32_t width, uint32_t height);
		// Cameraと参照元が使わなくなった出力を回収する
		void CollectRenderTextures(std::span<const ResolvedRenderView> cameras);
		size_t GetRenderTextureCount() const { return renderTextures_.size(); }
		uint64_t GetCollectedRenderTextureCount() const { return collectedRenderTextures_; }
		// 出力を直接参照する描画側の借用を保持する
		std::shared_ptr<const uint8_t> GetRenderTextureOwnership(AssetID assetID) const;

		//--------- accessor -----------------------------------------------------

		// 描画ビューのサーフェスの取得
		MultiRenderTarget* GetSurface(RenderViewKind kind);
		const MultiRenderTarget* GetSurface(RenderViewKind kind) const;
		MultiRenderTarget* GetRenderTextureSurface(AssetID assetID);
		const MultiRenderTarget* GetRenderTextureSurface(AssetID assetID) const;

		// 描画ビューのサーフェスの色レンダーテクスチャの取得
		RenderTexture2D* GetDisplayTexture(RenderViewKind kind, size_t colorIndex = 0);
		const RenderTexture2D* GetDisplayTexture(RenderViewKind kind, size_t colorIndex = 0) const;

		// 最後に描画した画像の輝度基準を保持する
		void SetDisplayOutput(RenderViewKind kind, const DisplayOutputSettings& output) { GetSlot(kind).displayOutput = output; }
		const DisplayOutputSettings& GetDisplayOutput(RenderViewKind kind) const { return GetSlot(kind).displayOutput; }

		// 描画ビューのエイリアスの取得
		static const char* GetViewAlias(RenderViewKind kind);
		//描画ビューの名前取得
		static const char* GetPrimaryColorName(RenderViewKind kind);
		static const char* GetPrimaryDepthName(RenderViewKind kind);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// 描画ビューのスロット
		struct SurfaceSlot {

			DisplayOutputSettings displayOutput{};
			uint32_t width = 0;
			uint32_t height = 0;
			std::unique_ptr<MultiRenderTarget> surface;
			std::shared_ptr<const uint8_t> ownership;
			uint64_t lastUsedSerial = 0;
		};

		//--------- variables ----------------------------------------------------

		// 各ビューのスロット
		SurfaceSlot game_;
		SurfaceSlot scene_;
		std::unordered_map<AssetID, SurfaceSlot> renderTextures_{};
		uint64_t collectedRenderTextures_ = 0;

		//--------- functions ----------------------------------------------------

		SurfaceSlot& GetSlot(RenderViewKind kind);
		const SurfaceSlot& GetSlot(RenderViewKind kind) const;
		static void ReleaseSlot(SurfaceSlot& slot);

		// 描画ビューのサーフェスの情報を構築する
		static MultiRenderTargetCreateDesc BuildDefaultDesc(RenderViewKind kind, uint32_t width, uint32_t height);
		static MultiRenderTargetCreateDesc BuildRenderTextureDesc(AssetID assetID,
			uint32_t width, uint32_t height);
	};
} // Engine

