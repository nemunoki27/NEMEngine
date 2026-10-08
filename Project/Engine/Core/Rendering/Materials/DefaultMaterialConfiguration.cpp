#include "DefaultMaterialConfiguration.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

//============================================================================
//	DefaultMaterialConfigurationIO functions
//============================================================================
bool Engine::DefaultMaterialConfigurationIO::Read(
	const std::filesystem::path& path, DefaultMaterialConfiguration& output) {

	DefaultMaterialConfiguration loaded;
	std::error_code error;
	bool exists = std::filesystem::exists(path, error);
	if (error) {
		return false;
	}
	if (exists) {

		nlohmann::json data;
		if (!JsonFile::TryLoad(path, data) || !data.is_object()) {
			return false;
		}
		try {
			// 未解決の参照もGUIDのまま読み込む
			loaded.mesh = ParseAssetID(data, "mesh");
			loaded.sprite = ParseAssetID(data, "sprite");
			loaded.text = ParseAssetID(data, "text");
			loaded.line = ParseAssetID(data, "line");
			loaded.primitive = ParseAssetID(data, "primitive");
			loaded.primitive2D = ParseAssetID(data, "primitive2D");
			loaded.raytracingReflection = ParseAssetID(data, "raytracingReflection");
		} catch (const nlohmann::json::exception&) {
			return false;
		}
	}
	// 全項目の読込後に設定を渡す
	output = loaded;
	return true;
}

bool Engine::DefaultMaterialConfigurationIO::Write(
	const std::filesystem::path& path, const DefaultMaterialConfiguration& configuration) {

	if (path.empty()) {
		return false;
	}
	// 保存先フォルダーを用意する
	std::error_code error;
	if (!path.parent_path().empty()) {

		std::filesystem::create_directories(path.parent_path(), error);
		if (error) {
			return false;
		}
	}
	nlohmann::json data = nlohmann::json::object();
	data["mesh"] = ToAssetReferenceJson(configuration.mesh);
	data["sprite"] = ToAssetReferenceJson(configuration.sprite);
	data["text"] = ToAssetReferenceJson(configuration.text);
	data["line"] = ToAssetReferenceJson(configuration.line);
	data["primitive"] = ToAssetReferenceJson(configuration.primitive);
	data["primitive2D"] = ToAssetReferenceJson(configuration.primitive2D);
	data["raytracingReflection"] = ToAssetReferenceJson(configuration.raytracingReflection);
	return JsonFile::Save(path, data);
}
