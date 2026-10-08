#include "RuntimeTextureResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <format>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

//============================================================================
//	RuntimeTextureResolver internal
//============================================================================
namespace {

	// Textureと公開用GPU情報を一組で保持する
	struct RenderTexturePublication {

		Engine::RenderTexture2D* texture = nullptr;
		Engine::GPUTextureResource view{};
	};
	static_assert(std::is_nothrow_move_assignable_v<RenderTexturePublication>);
	std::unordered_map<Engine::AssetID, RenderTexturePublication> renderTextures;
	std::unordered_set<Engine::AssetID> feedbackDiagnostics;
	Engine::AssetID writingRenderTexture{};
	uint64_t bindingRevision = 1;

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

	void RegisterRenderTexture(AssetID textureAssetID, RenderTexture2D* texture) {

		if (!textureAssetID || !texture || !texture->IsValid()) {
			return;
		}
		// 公開情報が揃うまでは旧Textureを維持する
		RenderTexturePublication publication{};
		publication.texture = texture;
		GPUTextureResource& view = publication.view;
		view.resource = texture->GetResource();
		view.gpuHandle = texture->GetSRVGPUHandle();
		view.srvIndex = texture->GetSRVIndex();
		view.textureName = "RenderTexture";
		view.valid = true;
		renderTextures.insert_or_assign(textureAssetID, std::move(publication));
		++bindingRevision;
	}

	void UnregisterRenderTexture(AssetID textureAssetID, const RenderTexture2D* texture) {

		const auto found = renderTextures.find(textureAssetID);
		if (found != renderTextures.end() && found->second.texture == texture) {
			renderTextures.erase(found);
			feedbackDiagnostics.erase(textureAssetID);
			++bindingRevision;
		}
	}

	void BeginRenderTextureWrite(AssetID textureAssetID) {

		writingRenderTexture = textureAssetID;
	}

	void EndRenderTextureWrite(AssetID textureAssetID) {

		if (writingRenderTexture == textureAssetID) {
			writingRenderTexture = {};
		}
	}

	uint64_t GetBindingRevision() {

		return bindingRevision;
	}

	AssetID GetWritingRenderTexture() {

		return writingRenderTexture;
	}

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
		if (!textureAssetID) {
			return fallback;
		}

		// Camera出力は通常Textureと同じSRVとして公開する
		const auto renderTexture = renderTextures.find(textureAssetID);
		if (renderTexture != renderTextures.end()) {

			if (textureAssetID == writingRenderTexture) {
				if (feedbackDiagnostics.emplace(textureAssetID).second) {
					Logger::Output(LogType::Engine, spdlog::level::warn,
						"Camera出力を同じ描画から参照したため代替Textureを使用します ID={}",
						ToString(textureAssetID));
				}
				return fallback;
			}
			if (DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand()) {
				renderTexture->second.texture->Transition(*dxCommand,
					static_cast<D3D12_RESOURCE_STATES>(
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
						D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
			}
			return &renderTexture->second.view;
		}
		if (!assetDatabase) {
			return fallback;
		}

		// Camera出力は画像ファイルとして読み込まない
		const AssetMeta* meta = assetDatabase->Find(textureAssetID);
		if (meta && meta->type == AssetType::RenderTexture) {
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

		// Cameraが出力したRenderTextureを同じAssetIDで解決
		const auto renderTexture = renderTextures.find(textureAssetID);
		if (renderTexture != renderTextures.end() &&
			renderTexture->second.texture->IsValid() &&
			textureAssetID != writingRenderTexture) {

			if (DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand()) {
				renderTexture->second.texture->Transition(*dxCommand,
					static_cast<D3D12_RESOURCE_STATES>(
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
						D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
			}
			return { renderTexture->second.texture->GetSRVIndex(), false };
		}

		const GPUTextureResource* fallback =
			graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
		const uint32_t fallbackIndex =
			fallback && fallback->srvIndex != UINT32_MAX ?
			fallback->srvIndex : UINT32_MAX;
		if (renderTexture != renderTextures.end()) {
			if (textureAssetID == writingRenderTexture &&
				feedbackDiagnostics.emplace(textureAssetID).second) {
				Logger::Output(LogType::Engine, spdlog::level::warn,
					"Camera出力を同じ描画から参照したため代替Textureを使用します ID={}",
					ToString(textureAssetID));
			}
			return { fallbackIndex, true };
		}
		if (!assetDatabase) {
			return { fallbackIndex, false };
		}

		// 出力先の生成待ちは次の描画で再解決する
		const AssetMeta* meta = assetDatabase->Find(textureAssetID);
		if (meta && meta->type == AssetType::RenderTexture) {
			return { fallbackIndex, true };
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

		if (!textureAssetID) {
			return false;
		}

		const auto renderTexture = renderTextures.find(textureAssetID);
		if (renderTexture != renderTextures.end() &&
			renderTexture->second.texture->IsValid()) {

			const D3D12_RESOURCE_DESC desc = renderTexture->second.texture->GetResource()->GetDesc();
			outSize = Vector2(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
			return true;
		}

		if (!assetDatabase) {
			return false;
		}

		// Camera出力の生成待ちは画像Importerへ渡さない
		const AssetMeta* meta = assetDatabase->Find(textureAssetID);
		if (meta && meta->type == AssetType::RenderTexture) {
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

}
