#include "PrefabDocument.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

namespace Engine::PrefabDocument {

	Engine::UUID ReadLocalFileID(const nlohmann::json& value) {

		if (!value.is_string()) {
			return Engine::UUID{};
		}
		const std::string raw = value.get<std::string>();
		return raw.empty() ? Engine::UUID{} : Engine::FromString16Hex(raw);
	}

	Engine::UUID ReadEntityLocalFileID(const nlohmann::json& entityJson) {

		if (!entityJson.is_object() || !entityJson.contains("LocalFileID")) {
			return Engine::UUID{};
		}
		return ReadLocalFileID(entityJson["LocalFileID"]);
	}

	bool HasPrefabLocalJointTarget(const nlohmann::json& component,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& prefabLocalToSceneLocal) {

		if (!component.is_object() || !component.contains("skinnedEntityLocalFileID")) {
			return false;
		}
		const Engine::UUID targetLocalFileID = ReadLocalFileID(component["skinnedEntityLocalFileID"]);
		return targetLocalFileID && prefabLocalToSceneLocal.contains(targetLocalFileID);
	}
}

bool Engine::PrefabDocument::Read(AssetDatabase& database, AssetID prefabAsset,
	std::filesystem::path& fullPath, nlohmann::json& fileJson) {

	// プレファブアセットが存在するか
	fullPath = database.ResolveFullPath(prefabAsset);
	if (fullPath.empty()) {
		return false;
	}

	// ファイルからnlohmann::json読み込み
	fileJson = JsonAdapter::Load(fullPath);
	if (!fileJson.is_object()) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[PrefabSystem] Prefabファイルを読み込めませんでした AssetID={} Path={}",
			ToString(prefabAsset), fullPath.string());
		return false;
	}
	const uint32_t schemaVersion = fileJson.value("SchemaVersion", 0u);
	if (
		schemaVersion < kMinimumPrefabSchemaVersion || schemaVersion > kPrefabSchemaVersion ||
		!fileJson.contains("Header") || !fileJson["Header"].is_object() ||
		!fileJson.contains("Entities") || !fileJson["Entities"].is_array()) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[PrefabSystem] Prefabファイルの形式が不正です AssetID={} Path={}",
			ToString(prefabAsset), fullPath.string());
		return false;
	}
	return true;
}
