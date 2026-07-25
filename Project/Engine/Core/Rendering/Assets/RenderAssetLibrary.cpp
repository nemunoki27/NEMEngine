#include "RenderAssetLibrary.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <optional>
#include <system_error>

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
	nlohmann::json data = JsonAdapter::Load(path, true);
	T asset{};
	if (!FromJson(data, asset)) {
		return nullptr;
	}
	// 自身のGUIDが空ならアセットIDで補完する
	if (!asset.guid) {
		asset.guid = assetID;
	}
	ResolveRuntimeReferences(asset);
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
	particleEffectCache_.clear();
}

void Engine::RenderAssetLibrary::ResolveRuntimeReferences(ShaderAsset& asset) {

	for (ShaderStageEntry& stage : asset.stages) {

		std::filesystem::path sourcePath{};
		if (const std::optional<AssetID> sourceID = TryParseUUID16Hex(stage.file)) {

			sourcePath = database_->ResolveFullPath(*sourceID);
		} else {

			sourcePath = database_->ResolveAssetPath(stage.file);
		}

		std::error_code ec;
		if (!sourcePath.empty() && std::filesystem::is_regular_file(sourcePath, ec)) {
			stage.file = Algorithm::PathToUTF8(sourcePath.lexically_normal());
		}
	}
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

const Engine::ParticleEffectAsset* Engine::RenderAssetLibrary::LoadParticleEffect(AssetID assetID) {

	return LoadCachedAsset(particleEffectCache_, assetID);
}
