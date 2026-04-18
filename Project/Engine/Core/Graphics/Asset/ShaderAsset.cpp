#include "ShaderAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Enum/EnumAdapter.h>

//============================================================================
//	ShaderAsset classMethods
//============================================================================

bool Engine::FromJson(const nlohmann::json& data, ShaderAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}

	outAsset = ShaderAsset{};
	outAsset.guid = ParseAssetID(data, "guid");
	outAsset.name = data.value("name", "UnnamedShader");

	if (data.contains("stages") && data["stages"].is_array()) {
		for (const auto& stageJson : data["stages"]) {

			if (!stageJson.is_object()) {
				continue;
			}

			ShaderStageEntry entry{};
			entry.stage = EnumAdapter<ShaderStage>::FromString(stageJson.value("stage", "None")).value_or(ShaderStage::None);
			entry.file = stageJson.value("file", "");
			entry.entry = stageJson.value("entry", "main");
			entry.profile = stageJson.value("profile", "");

			if (entry.stage == ShaderStage::None || entry.file.empty()) {
				continue;
			}
			outAsset.stages.emplace_back(std::move(entry));
		}
	}
	return true;
}

nlohmann::json Engine::ToJson(const ShaderAsset& asset) {

	nlohmann::json data = nlohmann::json::object();

	data["guid"] = ToString(asset.guid);
	data["name"] = asset.name;
	data["stages"] = nlohmann::json::array();
	for (const auto& stage : asset.stages) {

		nlohmann::json item = nlohmann::json::object();
		item["stage"] = EnumAdapter<ShaderStage>::ToString(stage.stage);
		item["file"] = stage.file;
		item["entry"] = stage.entry;
		item["profile"] = stage.profile;
		data["stages"].push_back(item);
	}
	return data;
}

const Engine::ShaderStageEntry* Engine::FindShaderStage(const ShaderAsset& asset, ShaderStage stage) {

	for (const auto& entry : asset.stages) {
		if (entry.stage == stage) {
			return &entry;
		}
	}
	return nullptr;
}