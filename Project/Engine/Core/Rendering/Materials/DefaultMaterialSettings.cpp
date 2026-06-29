#include "DefaultMaterialSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <filesystem>

//============================================================================
//	DefaultMaterialSettings classMethods
//============================================================================
Engine::DefaultMaterialSettings& Engine::DefaultMaterialSettings::GetInstance() {

	static DefaultMaterialSettings instance;
	return instance;
}

void Engine::DefaultMaterialSettings::Load(const std::string& configPath) {

	configPath_ = configPath;

	// ファイルが無ければ未設定のままにする
	if (!JsonAdapter::Check(configPath_, false)) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(configPath_, false);
	if (!data.is_object()) {
		return;
	}

	// GUID参照で保存しているので存在検証はせず読み込むだけにする
	mesh_ = ParseAssetID(data, "mesh");
	sprite_ = ParseAssetID(data, "sprite");
	text_ = ParseAssetID(data, "text");
	line_ = ParseAssetID(data, "line");
	fillMesh_ = ParseAssetID(data, "fillMesh");
}

void Engine::DefaultMaterialSettings::Save() const {

	// パス未設定なら保存しない
	if (configPath_.empty()) {
		return;
	}

	// GameAssets配下のConfigフォルダはまだ無いことがあるので作ってから書き出す
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(configPath_).parent_path(), ec);

	nlohmann::json data = nlohmann::json::object();
	data["mesh"] = ToAssetReferenceJson(mesh_);
	data["sprite"] = ToAssetReferenceJson(sprite_);
	data["text"] = ToAssetReferenceJson(text_);
	data["line"] = ToAssetReferenceJson(line_);
	data["fillMesh"] = ToAssetReferenceJson(fillMesh_);
	JsonAdapter::Save(configPath_, data);
}

Engine::AssetID Engine::DefaultMaterialSettings::GetMeshOrBuiltin() const {

	return mesh_ ? mesh_ : BuiltinAssets::Materials::DefaultMesh;
}

Engine::AssetID Engine::DefaultMaterialSettings::GetSpriteOrBuiltin() const {

	return sprite_ ? sprite_ : BuiltinAssets::Materials::DefaultSprite;
}

Engine::AssetID Engine::DefaultMaterialSettings::GetTextOrBuiltin() const {

	return text_ ? text_ : BuiltinAssets::Materials::DefaultText;
}

Engine::AssetID Engine::DefaultMaterialSettings::GetLineOrBuiltin() const {

	return line_ ? line_ : BuiltinAssets::Materials::DefaultLine;
}

Engine::AssetID Engine::DefaultMaterialSettings::GetFillMeshOrBuiltin() const {

	return fillMesh_ ? fillMesh_ : BuiltinAssets::Materials::DefaultFillMesh;
}
