#include "EditorLayoutStorage.h"

//============================================================================
//	include
//============================================================================
#include "EditorLayoutSerialization.h"
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

using namespace Engine::EditorLayoutSerialization;

void Engine::EditorLayoutStorage::LoadCatalog(const std::filesystem::path& path, bool imported,
	std::vector<EditorStoredLayout>& outLayouts, std::string* outDefaultLayoutID) {

	outLayouts.clear();
	if (!JsonAdapter::Check(path.string(), false)) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(path.string(), false);
	if (!data.is_object() || data.value("schemaVersion", 0) != kSchemaVersion) {
		return;
	}
	if (outDefaultLayoutID) {
		*outDefaultLayoutID = data.value("defaultLayoutID", *outDefaultLayoutID);
	}

	const nlohmann::json layouts = data.value("layouts", nlohmann::json::array());
	if (!layouts.is_array()) {
		return;
	}
	for (const nlohmann::json& layoutData : layouts) {

		EditorStoredLayout stored{};
		stored.imported = imported;
		if (ReadLayout(layoutData, stored.layout, stored.imported)) {
			outLayouts.emplace_back(std::move(stored));
		}
	}
}

void Engine::EditorLayoutStorage::SaveCatalog(const std::filesystem::path& path,
	const std::vector<EditorStoredLayout>& layouts, const std::string* defaultLayoutID) {

	nlohmann::json data = nlohmann::json::object();
	data["schemaVersion"] = kSchemaVersion;
	if (defaultLayoutID) {
		data["defaultLayoutID"] = *defaultLayoutID;
	}
	data["layouts"] = nlohmann::json::array();
	for (const EditorStoredLayout& stored : layouts) {
		data["layouts"].push_back(MakeLayoutJson(stored.layout, stored.imported));
	}
	JsonAdapter::Save(path.string(), data);
}
