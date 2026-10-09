#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

namespace Engine {

	class GraphicsCore;
	class RenderTexture2D;

	namespace RuntimeTextureResolver {
		struct BindlessResolveResult {

			uint32_t srvIndex = UINT32_MAX;
			bool retry = false;
		};

		// 実行中のRenderTextureをAssetIDへ対応付ける
		void RegisterRenderTexture(AssetID textureAssetID, RenderTexture2D* texture);
		void UnregisterRenderTexture(AssetID textureAssetID, const RenderTexture2D* texture);
		// 描画中のRenderTextureを入力として再利用しない
		void BeginRenderTextureWrite(AssetID textureAssetID);
		void EndRenderTextureWrite(AssetID textureAssetID);

		// RenderTextureの公開世代と描画先を取得する
		uint64_t GetBindingRevision();
		AssetID GetWritingRenderTexture();

		// アセットIDからGPUテクスチャリソースを解決し未ロードなら読み込み要求を行う
		const GPUTextureResource* Resolve(GraphicsCore& graphicsCore,
			const AssetDatabase* assetDatabase, AssetID textureAssetID,
			TextureColorSpace requestedColorSpace = TextureColorSpace::Auto);
		// bindless SRV indexを解決し、非同期ロード中なら次フレーム再試行を要求する
		BindlessResolveResult ResolveBindless(GraphicsCore& graphicsCore,
			const AssetDatabase* assetDatabase, AssetID textureAssetID,
			TextureColorSpace requestedColorSpace = TextureColorSpace::Auto, bool normalMap = false);
		// アセットの.metaから正規化済みImporter設定を取得する
		TextureImportSettings ResolveImportSettings(
			const AssetDatabase* assetDatabase, AssetID textureAssetID);

		// テクスチャの実ピクセルサイズを取得する、ロード済みのときのみtrueを返しフォールバックは対象外
		bool TryResolveSize(GraphicsCore& graphicsCore,
			const AssetDatabase* assetDatabase, AssetID textureAssetID, Vector2& outSize);

	} // RuntimeTextureResolver
} // Engine
