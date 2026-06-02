#include "RenderAssetLibrary.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

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
}

const Engine::ShaderAsset* Engine::RenderAssetLibrary::LoadShader(AssetID assetID) {

	if (!database_ || !assetID) {
		return nullptr;
	}

	// キャッシュデータを探す
	auto found = shaderCache_.find(assetID);
	if (found != shaderCache_.end()) {
		return &found->second;
	}

	// ファイルパスを検索
	const std::filesystem::path path = database_->ResolveFullPath(assetID);
	if (path.empty()) {
		return nullptr;
	}

	// データを読み込む
	nlohmann::json data = JsonAdapter::Load(path.string(), true);
	ShaderAsset asset{};
	if (!FromJson(data, asset)) {
		return nullptr;
	}
	if (!asset.guid) {
		asset.guid = assetID;
	}
	// キャッシュに保存
	auto [it, inserted] = shaderCache_.emplace(assetID, std::move(asset));
	return &it->second;
}

const Engine::RenderPipelineAsset* Engine::RenderAssetLibrary::LoadPipeline(AssetID assetID) {

	if (!database_ || !assetID) {
		return nullptr;
	}

	// キャッシュデータを探す
	auto found = pipelineCache_.find(assetID);
	if (found != pipelineCache_.end()) {
		return &found->second;
	}

	// ファイルパスを検索
	const std::filesystem::path path = database_->ResolveFullPath(assetID);
	if (path.empty()) {
		return nullptr;
	}

	// データを読み込む
	nlohmann::json data = JsonAdapter::Load(path.string(), true);
	RenderPipelineAsset asset{};
	if (!FromJson(data, asset)) {
		return nullptr;
	}
	if (!asset.guid) {
		asset.guid = assetID;
	}
	// キャッシュに保存
	auto [it, inserted] = pipelineCache_.emplace(assetID, std::move(asset));
	return &it->second;
}

const Engine::MaterialAsset* Engine::RenderAssetLibrary::LoadMaterial(AssetID assetID) {

	if (!database_ || !assetID) {
		return nullptr;
	}

	// キャッシュデータを探す
	auto found = materialCache_.find(assetID);
	if (found != materialCache_.end()) {
		return &found->second;
	}

	// ファイルパスを検索
	const std::filesystem::path path = database_->ResolveFullPath(assetID);
	if (path.empty()) {
		return nullptr;
	}

	// データを読み込む
	nlohmann::json data = JsonAdapter::Load(path.string(), true);
	MaterialAsset asset{};
	if (!FromJson(data, asset)) {
		return nullptr;
	}
	if (!asset.guid) {
		asset.guid = assetID;
	}
	auto [it, inserted] = materialCache_.emplace(assetID, std::move(asset));
	return &it->second;
}

const Engine::MSDFFontAsset* Engine::RenderAssetLibrary::LoadFont(AssetID assetID) {

	if (!database_ || !assetID) {
		return nullptr;
	}

	// キャッシュデータを探す
	auto found = fontCache_.find(assetID);
	if (found != fontCache_.end()) {
		return &found->second;
	}

	// ファイルパスを検索
	const std::filesystem::path path = database_->ResolveFullPath(assetID);
	if (path.empty()) {
		return nullptr;
	}

	// データを読み込む
	nlohmann::json data = JsonAdapter::Load(path.string(), true);
	MSDFFontAsset asset{};
	if (!FromJson(data, asset)) {
		return nullptr;
	}
	if (!asset.guid) {
		asset.guid = assetID;
	}

	auto [it, inserted] = fontCache_.emplace(assetID, std::move(asset));
	return &it->second;
}
