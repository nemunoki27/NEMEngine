#include "RenderAssetLibrary.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

//============================================================================
//	RenderAssetLibrary templateMethods
//============================================================================
template <typename T>
const T* Engine::RenderAssetLibrary::LoadCachedAsset(std::unordered_map<AssetID, T>& cache, AssetID assetID) {

	if (!database_ || !assetID) {
		return nullptr;
	}

	// キャッシュデータを探す
	auto found = cache.find(assetID);
	if (found != cache.end()) {
		return &found->second;
	}

	// ファイルパスを検索
	const std::filesystem::path path = database_->ResolveFullPath(assetID);
	if (path.empty()) {
		return nullptr;
	}

	// データを読み込む
	nlohmann::json data = JsonAdapter::Load(path.string(), true);
	T asset{};
	if (!FromJson(data, asset)) {
		return nullptr;
	}
	// 自身のGUIDが空ならアセットIDで補完する
	if (!asset.guid) {
		asset.guid = assetID;
	}
	// キャッシュに保存
	auto [it, inserted] = cache.emplace(assetID, std::move(asset));
	return &it->second;
}

//============================================================================
//	RenderAssetLibrary classMethods
//============================================================================
void Engine::RenderAssetLibrary::Init(AssetDatabase* database) {

	if (database_) {
		return;
	}

	database_ = database;
	Clear();
}

void Engine::RenderAssetLibrary::Clear() {

	shaderCache_.clear();
	pipelineCache_.clear();
	materialCache_.clear();
	fontCache_.clear();
}

const Engine::ShaderAsset* Engine::RenderAssetLibrary::LoadShader(AssetID assetID) {

	return LoadCachedAsset(shaderCache_, assetID);
}

const Engine::RenderPipelineAsset* Engine::RenderAssetLibrary::LoadPipeline(AssetID assetID) {

	return LoadCachedAsset(pipelineCache_, assetID);
}

const Engine::MaterialAsset* Engine::RenderAssetLibrary::LoadMaterial(AssetID assetID) {

	return LoadCachedAsset(materialCache_, assetID);
}

const Engine::MSDFFontAsset* Engine::RenderAssetLibrary::LoadFont(AssetID assetID) {

	return LoadCachedAsset(fontCache_, assetID);
}
