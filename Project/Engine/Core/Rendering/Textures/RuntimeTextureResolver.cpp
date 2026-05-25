#include "RuntimeTextureResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>

namespace Engine::RuntimeTextureResolver {

	const GPUTextureResource* Resolve(GraphicsCore& graphicsCore,
		AssetDatabase* assetDatabase, AssetID textureAssetID, bool sRGB) {

		// フォールバック用のエラーテクスチャを取得
		const GPUTextureResource* fallback = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
		if (!fallback || !fallback->valid) {
			return nullptr;
		}

		// テクスチャ未指定ならエラーテクスチャ
		if (!textureAssetID || !assetDatabase) {
			return fallback;
		}

		// アセットIDからフルパスを取得
		std::filesystem::path fullPath = assetDatabase->ResolveFullPath(textureAssetID);
		if (fullPath.empty()) {
			return fallback;
		}

		TextureUploadService& uploadService = graphicsCore.GetTextureUploadService();
		const std::string basePath = fullPath.generic_string();
		const std::string key = sRGB ? basePath + ":srgb" : basePath;

		// 未リクエストならリクエストを投げる
		if (uploadService.GetState(key) == TextureRequestState::None) {

			TextureFileRequestDesc desc{};
			desc.key = key;
			desc.assetPath = basePath;
			desc.forceSRGB = sRGB;
			uploadService.RequestTextureFile(desc);
		}

		// ロード完了していれば返す
		if (const auto* texture = uploadService.GetTexture(key)) {
			if (texture->valid) {
				return texture;
			}
		}

		return fallback;
	}

} // Engine::RuntimeTextureResolver
