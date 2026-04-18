#include "RenderAssetLibrary.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Json/JsonAdapter.h>

//============================================================================
//	RenderAssetLibrary classMethods
//============================================================================

namespace {

	bool LooksLikeAssetID(const std::string& text) {

		if (text.size() != 16) {
			return false;
		}
		for (char c : text) {
			if (!std::isxdigit(static_cast<unsigned char>(c))) {
				return false;
			}
		}
		return true;
	}

	Engine::AssetID ParseGuessAssetReference(
		Engine::AssetDatabase& database,
		const nlohmann::json& value,
		Engine::AssetType guessedType) {

		if (!value.is_string()) {
			return Engine::AssetID{};
		}

		const std::string text = value.get<std::string>();
		if (text.empty()) {
			return Engine::AssetID{};
		}

		if (LooksLikeAssetID(text)) {
			return Engine::FromString16Hex(text);
		}

		const std::filesystem::path fullPath = database.GetProjectRoot() / text;
		if (!std::filesystem::exists(fullPath)) {
			return Engine::AssetID{};
		}

		return database.ImportOrGet(text, guessedType);
	}
}

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
	if (data.contains("variants") && data["variants"].is_array()) {
		for (auto& variantJson : data["variants"]) {

			if (!variantJson.is_object()) {
				continue;
			}
			const AssetID shaderRef = ParseGuessAssetReference(*database_, variantJson.value("shader", ""), AssetType::Shader);
			if (shaderRef) {
				variantJson["shader"] = ToString(shaderRef);
			}
		}
	}
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
	if (data.contains("passes") && data["passes"].is_array()) {
		for (auto& passJson : data["passes"]) {

			if (!passJson.is_object()) {
				continue;
			}

			const AssetID pipelineRef = ParseGuessAssetReference(*database_, passJson.value("pipeline", ""), AssetType::RenderPipeline);
			if (pipelineRef) {
				passJson["pipeline"] = ToString(pipelineRef);
			}
		}
	}
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
	if (data.contains("atlasTexture")) {
		const AssetID atlasRef = ParseGuessAssetReference(*database_, data["atlasTexture"], AssetType::Texture);
		if (atlasRef) {
			data["atlasTexture"] = ToString(atlasRef);
		}
	}
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