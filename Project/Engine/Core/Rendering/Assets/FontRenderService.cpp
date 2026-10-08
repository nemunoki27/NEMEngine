#include "FontRenderService.h"

//============================================================================
//	include
//============================================================================
#include "RenderAssetLibrary.h"
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <limits>
#include <stdexcept>

//============================================================================
//	FontRenderService classMethods
//============================================================================
Engine::FontRenderService::FontRenderService(TextureUploadService& textures) : textures_(textures) {}

Engine::FontRenderService::~FontRenderService() {

	Clear();
}

Engine::FontRenderGeneration Engine::FontRenderService::Resolve(RenderAssetLibrary& library, AssetID assetID) {

	auto& entry = libraries_[library.GetFontCacheIdentity()][assetID];
	if (entry.requested) {
		const auto revision = textures_.GetFileReloadRevision(Algorithm::PathFromUTF8(entry.requested->atlas.assetPath));
		if (revision != entry.atlasReloadRevision) {
			// Atlasの単体更新もFontと揃えた次の世代へ渡す
			library.InvalidateFont(assetID);
			entry.atlasReloadRevision = revision;
		}
	}
	const auto source = library.LoadFontSource(assetID);
	if (source && source != entry.requested) {
		// 進行中の旧要求を失効させ、新しい世代だけ転送する
		const auto key = textures_.RequestSnapshot(source->atlas);
		textures_.ReleaseSnapshot(entry.pendingKey);
		entry.pendingKey = key;
		entry.requested = source;
		entry.atlasReloadRevision = textures_.GetFileReloadRevision(Algorithm::PathFromUTF8(source->atlas.assetPath));
	}
	if (!entry.pendingKey.empty()) {
		const auto state = textures_.GetState(entry.pendingKey);
		const auto* texture = textures_.GetTexture(entry.pendingKey);
		if (state == TextureRequestState::Ready && texture && texture->valid) {
			const auto description = texture->resource->GetDesc();
			if (description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && description.DepthOrArraySize == 1 &&
				description.Width == entry.requested->font.atlasWidth &&
				description.Height == entry.requested->font.atlasHeight) {
				if (nextContentRevision_ == std::numeric_limits<uint64_t>::max()) {
					throw std::overflow_error("Fontの描画世代が上限に達しました");
				}
				// 別Libraryのレイアウトも同じ内容番号で再利用しない
				auto published = std::make_shared<MSDFFontAsset>(entry.requested->font);
				published->contentRevision = nextContentRevision_;
				// 配置情報と画像を同時に公開し、旧Descriptorを退避する
				textures_.ReleaseSnapshot(entry.publishedKey);
				entry.publishedKey = std::move(entry.pendingKey);
				entry.pendingKey.clear();
				entry.published = std::move(published);
				++nextContentRevision_;
			} else {
				Logger::Output(
					LogType::Engine, spdlog::level::warn, "[Font読込] Atlasサイズが一致しません ID={}", ToString(assetID));
				textures_.ReleaseSnapshot(entry.pendingKey);
				entry.pendingKey.clear();
			}
		} else if (state == TextureRequestState::Failed || state == TextureRequestState::None) {
			// 失敗した世代は再読込要求まで繰り返さない
			textures_.ReleaseSnapshot(entry.pendingKey);
			entry.pendingKey.clear();
		}
	}
	return entry.published ? FontRenderGeneration{entry.published.get(), textures_.GetTexture(entry.publishedKey)}
						   : FontRenderGeneration{};
}

void Engine::FontRenderService::Release(Entries& entries) {

	for (auto& [assetID, entry] : entries) {
		textures_.ReleaseSnapshot(entry.pendingKey);
		textures_.ReleaseSnapshot(entry.publishedKey);
	}
}

void Engine::FontRenderService::CollectExpired() {

	for (auto entry = libraries_.begin(); entry != libraries_.end();) {
		if (!entry->first.expired()) {
			++entry;
			continue;
		}
		// World切替後もGPU使用中の画像は回収窓口に残す
		Release(entry->second);
		entry = libraries_.erase(entry);
	}
}

void Engine::FontRenderService::Clear() {

	for (auto& [identity, entries] : libraries_) {
		Release(entries);
	}
	libraries_.clear();
}
