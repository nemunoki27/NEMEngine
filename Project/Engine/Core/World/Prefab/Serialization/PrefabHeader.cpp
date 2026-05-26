#include "PrefabHeader.h"

//============================================================================
//	PrefabHeader classMethods
//============================================================================

bool Engine::FromJson(const nlohmann::json& data, PrefabHeader& prefabHeader) {

	if (!data.is_object()) {
		return false;
	}

	std::string guidStr = data.value("guid", "");
	std::string rootLocalFileIDStr = data.value("rootLocalFileID", "");

	prefabHeader.guid = guidStr.empty() ? UUID::New() : FromString16Hex(guidStr);
	prefabHeader.name = data.value("name", "UntitledPrefab");
	prefabHeader.rootLocalFileID = rootLocalFileIDStr.empty() ? UUID{} : FromString16Hex(rootLocalFileIDStr);
	prefabHeader.version = data.value("version", 1u);
	return true;
}

nlohmann::json Engine::ToJson(const PrefabHeader& prefabHeader) {

	nlohmann::json data = nlohmann::json::object();

	data["guid"] = prefabHeader.guid ? ToString(prefabHeader.guid) : "";
	data["name"] = prefabHeader.name;
	data["rootLocalFileID"] = prefabHeader.rootLocalFileID ? ToString(prefabHeader.rootLocalFileID) : "";
	data["version"] = prefabHeader.version;

	return data;
}