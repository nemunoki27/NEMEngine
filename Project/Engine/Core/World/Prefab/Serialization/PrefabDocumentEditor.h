#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>

namespace Engine::PrefabDocumentEditor {

	// 指定実体のプロパティ値を変更する
	bool SetPrefabEntityLeaf(nlohmann::json& prefabFileJson, UUID targetLocalFileID,
		const std::string& path, const nlohmann::json& value);

	// 指定実体のComponent保存値を変更する
	bool SetPrefabEntityComponent(nlohmann::json& prefabFileJson, UUID targetLocalFileID,
		const std::string& type, const nlohmann::json& value);

	// 指定実体のComponent保存値を削除する
	bool RemovePrefabEntityComponent(nlohmann::json& prefabFileJson, UUID targetLocalFileID, const std::string& type);
}
