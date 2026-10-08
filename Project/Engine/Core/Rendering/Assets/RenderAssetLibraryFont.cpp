#include "RenderAssetLibrary.h"

//============================================================================
//	include
//============================================================================
#include "FontSourceSnapshot.h"
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <cmath>
#include <limits>
#include <stdexcept>

//============================================================================
//	RenderAssetLibrary classMethods
//============================================================================
std::shared_ptr<const Engine::FontSourceSnapshot> Engine::RenderAssetLibrary::LoadFontSource(AssetID assetID) {

	if (!database_ || !assetID) {
		return {};
	}
	const bool attempted = fontCache_.contains(assetID);
	auto& cached = fontCache_[assetID];
	const bool invalidated = invalidatedFonts_.erase(assetID) != 0;
	if (attempted && !invalidated) {
		return cached;
	}
	try {
		// 配置情報と画像をファイル変更前のバイト列で保持する
		const auto path = database_->ResolveFullPath(assetID);
		const auto revision = StorageFileUtility::FileRevision(path);
		const auto bytes = StorageFileUtility::ReadVerifiedBytes(path, revision);
		auto source = std::make_shared<FontSourceSnapshot>();
		if (!FromJson(nlohmann::json::parse(bytes), source->font) || !source->font.atlasTexture ||
			source->font.glyphMap.empty() || !std::isfinite(source->font.metrics.elementSize) ||
			source->font.metrics.elementSize <= 0.0f) {
			throw std::runtime_error("Fontの配置情報が無効です");
		}
		const auto atlasPath = database_->ResolveFullPath(source->font.atlasTexture);
		const auto atlasRevision = StorageFileUtility::FileRevision(atlasPath);
		source->atlas.assetPath = Algorithm::PathToUTF8(atlasPath);
		source->atlas.snapshotBytes =
			std::make_shared<const std::string>(StorageFileUtility::ReadVerifiedBytes(atlasPath, atlasRevision));
		if (source->atlas.snapshotBytes->empty() || StorageFileUtility::FileRevision(path) != revision) {
			throw std::runtime_error("Font読込中に配置情報が変更されました");
		}
		if (const AssetMeta* atlasMeta = database_->Find(source->font.atlasTexture)) {
			source->atlas.importSettings = ParseTextureImportSettings(atlasMeta->importerSettings);
		}
		if (nextFontContentRevision_ == std::numeric_limits<uint64_t>::max()) {
			throw std::overflow_error("Fontの内容世代が上限に達しました");
		}
		// 一組の読込成功後だけ旧データを置き換える
		source->font.guid = assetID;
		source->font.contentRevision = nextFontContentRevision_++;
		cached = std::move(source);
	} catch (const std::exception& exception) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "[Font読込] ID={} 原因={}", ToString(assetID), exception.what());
	}
	return cached;
}

const Engine::MSDFFontAsset* Engine::RenderAssetLibrary::LoadFont(AssetID assetID) {

	const auto source = LoadFontSource(assetID);
	return source ? &source->font : nullptr;
}

void Engine::RenderAssetLibrary::InvalidateFont(AssetID assetID) {

	invalidatedFonts_.insert(assetID);
}
