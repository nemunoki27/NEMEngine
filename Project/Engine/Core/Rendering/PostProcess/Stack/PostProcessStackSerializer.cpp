#include "PostProcessStackSerializer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/IDentity/UUID.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <filesystem>

//============================================================================
//	PostProcessStackSerializer classMethods
//============================================================================
bool Engine::PostProcessStackSerializer::Load(const std::filesystem::path& path, PostProcessStackSettings& outSettings) {

	if (!std::filesystem::exists(path)) {
		return false;
	}

	const nlohmann::json data = JsonAdapter::Load(path.string(), false);
	if (!data.is_object()) {
		return false;
	}

	outSettings = FromJson(data);
	return true;
}

bool Engine::PostProcessStackSerializer::Save(const std::filesystem::path& path, const PostProcessStackSettings& settings) {

	const std::filesystem::path dir = path.parent_path();
	if (!dir.empty() && !std::filesystem::exists(dir)) {
		std::filesystem::create_directories(dir);
	}

	JsonAdapter::Save(path.string(), ToJson(settings));
	return true;
}

Engine::PostProcessStackSettings Engine::PostProcessStackSerializer::FromJson(const nlohmann::json& data) {

	PostProcessStackSettings settings{};
	settings.version = data.value("version", 1);

	if (!data.contains("passes") || !data["passes"].is_array()) {
		return settings;
	}

	for (const auto& passJson : data["passes"]) {

		if (!passJson.is_object()) {
			continue;
		}

		PostProcessStackPassSettings pass{};
		const std::string idStr = passJson.value("id", "");
		pass.id = idStr.size() == 16 ? FromString16Hex(idStr) : UUID::New();
		pass.name = passJson.value("name", "Pass");
		pass.enabled = passJson.value("enabled", true);

		const std::string guidStr = passJson.value("materialGuid", "");
		pass.materialGuid = guidStr.size() == 16 ? FromString16Hex(guidStr) : AssetID{};
		const auto passKind = EnumAdapter<MaterialPassKind>::FromString(passJson.value("passKind", ""));
		if (!passKind || *passKind == MaterialPassKind::Invalid) {
			continue;
		}
		pass.passKind = *passKind;

		if (passJson.contains("parameters") && passJson["parameters"].is_object()) {
			for (auto it = passJson["parameters"].begin(); it != passJson["parameters"].end(); ++it) {
				MaterialParameterValue value{};
				if (Engine::ParseMaterialParameterValue(it.value(), value)) {
					pass.parameterOverrides[it.key()] = std::move(value);
				}
			}
		}

		if (passJson.contains("textures") && passJson["textures"].is_object()) {
			for (auto it = passJson["textures"].begin(); it != passJson["textures"].end(); ++it) {
				if (!it.value().is_object()) {
					continue;
				}
				const std::string texGuidStr = it.value().value("textureGuid", "");
				if (texGuidStr.size() == 16) {
					pass.textureGuids[it.key()] = FromString16Hex(texGuidStr);
				}
			}
		}

		settings.passes.emplace_back(std::move(pass));
	}

	return settings;
}

nlohmann::json Engine::PostProcessStackSerializer::ToJson(const PostProcessStackSettings& settings) {

	nlohmann::json data = nlohmann::json::object();
	data["version"] = settings.version;

	data["passes"] = nlohmann::json::array();
	for (const auto& pass : settings.passes) {

		nlohmann::json passJson = nlohmann::json::object();
		passJson["id"] = ToString(pass.id);
		passJson["name"] = pass.name;
		passJson["enabled"] = pass.enabled;
		passJson["materialGuid"] = ToAssetReferenceJson(pass.materialGuid);
		passJson["passKind"] = EnumAdapter<MaterialPassKind>::ToString(pass.passKind);

		passJson["parameters"] = nlohmann::json::object();
		for (const auto& [name, value] : pass.parameterOverrides) {
			passJson["parameters"][name] = Engine::SerializeMaterialParameterValue(value);
		}

		passJson["textures"] = nlohmann::json::object();
		for (const auto& [name, guid] : pass.textureGuids) {
			nlohmann::json texJson = nlohmann::json::object();
			texJson["textureGuid"] = ToAssetReferenceJson(guid);
			passJson["textures"][name] = texJson;
		}

		data["passes"].push_back(std::move(passJson));
	}

	return data;
}
