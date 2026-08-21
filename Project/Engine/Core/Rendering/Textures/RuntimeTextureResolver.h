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

	namespace RuntimeTextureResolver {
		struct BindlessResolveResult {

			uint32_t srvIndex = UINT32_MAX;
			bool retry = false;
		};

		// アセットIDからGPUテクスチャリソースを解決し未ロードなら読み込み要求を行う
		const GPUTextureResource* Resolve(GraphicsCore& graphicsCore,
			const AssetDatabase* assetDatabase, AssetID textureAssetID,
			TextureColorSpace requestedColorSpace = TextureColorSpace::Auto);
		// bindless SRV indexを解決し、非同期ロード中なら次フレーム再試行を要求する
		BindlessResolveResult ResolveBindless(GraphicsCore& graphicsCore,
			const AssetDatabase* assetDatabase, AssetID textureAssetID,
			TextureColorSpace requestedColorSpace = TextureColorSpace::Auto);
		// アセットの.metaから正規化済みImporter設定を取得する
		TextureImportSettings ResolveImportSettings(
			const AssetDatabase* assetDatabase, AssetID textureAssetID);

		// テクスチャの実ピクセルサイズを取得する、ロード済みのときのみtrueを返しフォールバックは対象外
		bool TryResolveSize(GraphicsCore& graphicsCore,
			const AssetDatabase* assetDatabase, AssetID textureAssetID, Vector2& outSize);

	} // RuntimeTextureResolver
} // Engine
