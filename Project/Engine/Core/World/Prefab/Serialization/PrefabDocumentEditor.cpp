#include "PrefabDocumentEditor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabJsonDiff.h>

// c++

namespace {

	nlohmann::json* FindPrefabEntityComponents(nlohmann::json& prefabFileJson, Engine::UUID targetLocalFileID) {

		if (!prefabFileJson.is_object() || !prefabFileJson.contains("Entities") ||
			!prefabFileJson["Entities"].is_array()) {
			return nullptr;
		}
		const std::string targetStr = Engine::ToString(targetLocalFileID);
		for (auto& entityJson : prefabFileJson["Entities"]) {

			const std::string localStr = entityJson.value("LocalFileID", std::string{});
			if (localStr != targetStr) {
				continue;
			}
			if (!entityJson.contains("Components") || !entityJson["Components"].is_object()) {
				entityJson["Components"] = nlohmann::json::object();
			}
			return &entityJson["Components"];
		}
		return nullptr;
	}
}

bool Engine::PrefabDocumentEditor::SetPrefabEntityLeaf(nlohmann::json& prefabFileJson, UUID targetLocalFileID,
	const std::string& path, const nlohmann::json& value) {

	nlohmann::json* components = FindPrefabEntityComponents(prefabFileJson, targetLocalFileID);
	if (!components) {
		return false;
	}
	// 経路の先頭セグメントが型名、残りがコンポーネント内のリーフ経路
	const size_t slash = path.find('/');
	const std::string type = (slash == std::string::npos) ? path : path.substr(0, slash);
	const std::string leaf = (slash == std::string::npos) ? std::string{} : path.substr(slash + 1);
	if (!(*components).contains(type) || !(*components)[type].is_object()) {
		(*components)[type] = nlohmann::json::object();
	}
	PrefabJsonDiff::SetAtPath((*components)[type], leaf, value);
	return true;
}

bool Engine::PrefabDocumentEditor::SetPrefabEntityComponent(nlohmann::json& prefabFileJson, UUID targetLocalFileID,
	const std::string& type, const nlohmann::json& value) {

	nlohmann::json* components = FindPrefabEntityComponents(prefabFileJson, targetLocalFileID);
	if (!components) {
		return false;
	}
	(*components)[type] = value;
	return true;
}

bool Engine::PrefabDocumentEditor::RemovePrefabEntityComponent(nlohmann::json& prefabFileJson,
	UUID targetLocalFileID, const std::string& type) {

	nlohmann::json* components = FindPrefabEntityComponents(prefabFileJson, targetLocalFileID);
	if (!components) {
		return false;
	}
	components->erase(type);
	return true;
}
