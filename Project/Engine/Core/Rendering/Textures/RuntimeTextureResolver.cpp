#include "RuntimeTextureResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <format>

//============================================================================
//	RuntimeTextureResolver internal
//============================================================================
namespace {

	// Importer色空間が明示済みなら描画用途に依存しない同一GPUリソースへ統合する
	std::string MakeTextureKey(const std::string& path,
		const Engine::TextureImportSettings& importSettings,
		Engine::TextureColorSpace requestedColorSpace) {

		const Engine::TextureColorSpace cacheColorSpace =
			importSettings.colorSpace == Engine::TextureColorSpace::Auto ?
			requestedColorSpace : Engine::TextureColorSpace::Auto;
		return std::format("{}:texture:{}", path,
			static_cast<uint32_t>(cacheColorSpace));
	}
}

namespace Engine::RuntimeTextureResolver {

	TextureImportSettings ResolveImportSettings(
		const AssetDatabase* assetDatabase, AssetID textureAssetID) {

		if (!assetDatabase || !textureAssetID) {
			return MakeTextureImportSettings(TextureImportPreset::Default);
		}
		const AssetMeta* meta = assetDatabase->Find(textureAssetID);
		return meta ? ParseTextureImportSettings(meta->importerSettings) :
			MakeTextureImportSettings(TextureImportPreset::Default);
	}

	const GPUTextureResource* Resolve(GraphicsCore& graphicsCore,
		const AssetDatabase* assetDatabase, AssetID textureAssetID,
		TextureColorSpace requestedColorSpace) {

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
		const std::string basePath = Algorithm::PathToUTF8(fullPath);
		const TextureImportSettings importSettings = ResolveImportSettings(
			assetDatabase, textureAssetID);
		const std::string key = MakeTextureKey(
			basePath, importSettings, requestedColorSpace);

		// 未リクエストならリクエストを投げる
		if (uploadService.GetState(key) == TextureRequestState::None) {

			TextureFileRequestDesc desc{};
			desc.key = key;
			desc.assetPath = basePath;
			desc.importSettings = importSettings;
			desc.requestedColorSpace = requestedColorSpace;
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

	BindlessResolveResult ResolveBindless(GraphicsCore& graphicsCore,
		const AssetDatabase* assetDatabase, AssetID textureAssetID,
		TextureColorSpace requestedColorSpace) {

		if (!textureAssetID) {
			return {};
		}

		const GPUTextureResource* fallback =
			graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
		const uint32_t fallbackIndex =
			fallback && fallback->srvIndex != UINT32_MAX ?
			fallback->srvIndex : UINT32_MAX;
		if (!assetDatabase) {
			return { fallbackIndex, false };
		}

		const std::filesystem::path fullPath =
			assetDatabase->ResolveFullPath(textureAssetID);
		if (fullPath.empty()) {
			return { fallbackIndex, false };
		}

		TextureUploadService& uploadService =
			graphicsCore.GetTextureUploadService();
		const std::string basePath = Algorithm::PathToUTF8(fullPath);
		const TextureImportSettings importSettings = ResolveImportSettings(
			assetDatabase, textureAssetID);
		const std::string key = MakeTextureKey(
			basePath, importSettings, requestedColorSpace);
		TextureRequestState state = uploadService.GetState(key);
		if (state == TextureRequestState::None) {

			TextureFileRequestDesc desc{};
			desc.key = key;
			desc.assetPath = basePath;
			desc.importSettings = importSettings;
			desc.requestedColorSpace = requestedColorSpace;
			uploadService.RequestTextureFile(desc);
			state = TextureRequestState::Queued;
		}

		if (const GPUTextureResource* texture =
			uploadService.GetTexture(key)) {
			if (texture->valid && texture->srvIndex != UINT32_MAX) {
				return { texture->srvIndex, false };
			}
		}
		return { fallbackIndex, state != TextureRequestState::Failed };
	}

	bool TryResolveSize(GraphicsCore& graphicsCore,
		const AssetDatabase* assetDatabase, AssetID textureAssetID, Vector2& outSize) {

		if (!textureAssetID || !assetDatabase) {
			return false;
		}

		// アセットIDからフルパスを取得
		std::filesystem::path fullPath = assetDatabase->ResolveFullPath(textureAssetID);
		if (fullPath.empty()) {
			return false;
		}

		TextureUploadService& uploadService = graphicsCore.GetTextureUploadService();
		const std::string basePath = Algorithm::PathToUTF8(fullPath);
		const TextureImportSettings importSettings = ResolveImportSettings(
			assetDatabase, textureAssetID);
		const std::string key = MakeTextureKey(
			basePath, importSettings, TextureColorSpace::Auto);

		// 未リクエストなら読み込みを促す、ロード済みになるまでは実サイズが確定しない
		if (uploadService.GetState(key) == TextureRequestState::None) {

			TextureFileRequestDesc desc{};
			desc.key = key;
			desc.assetPath = basePath;
			desc.importSettings = importSettings;
			uploadService.RequestTextureFile(desc);
		}

		// Resolveのフォールバック(エラーテクスチャ)を拾わないよう、ロード済みの実体だけを対象にする
		const GPUTextureResource* texture = uploadService.GetTexture(key);
		if (!texture || !texture->valid || !texture->resource) {
			return false;
		}

		const D3D12_RESOURCE_DESC desc = texture->resource->GetDesc();
		outSize = Vector2(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
		return true;
	}

} // Engine::RuntimeTextureResolver
