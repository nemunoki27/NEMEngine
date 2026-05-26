#include "PostProcessStackSerializer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/IDentity/UUID.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <filesystem>

//============================================================================
//	PostProcessStackSerializer classMethods
//============================================================================

namespace {

	bool TryParseParameterValue(const nlohmann::json& data, Engine::MaterialParameterValue& outValue) {

		if (data.is_number_float()) {
			outValue.value = data.get<float>();
			return true;
		}
		if (data.is_boolean()) {
			outValue.value = data.get<bool>();
			return true;
		}
		if (data.is_number_integer()) {
			outValue.value = static_cast<int32_t>(data.get<int64_t>());
			return true;
		}
		if (data.is_number_unsigned()) {
			outValue.value = static_cast<uint32_t>(data.get<uint64_t>());
			return true;
		}
		if (data.is_string()) {
			const std::string text = data.get<std::string>();
			if (text.size() == 16) {
				outValue.value = Engine::FromString16Hex(text);
				return true;
			}
			return false;
		}
		if (data.is_array()) {
			if (data.size() == 2 && data[0].is_number() && data[1].is_number()) {
				outValue.value = Engine::Vector2(data[0].get<float>(), data[1].get<float>());
				return true;
			}
			if (data.size() == 3 && data[0].is_number() && data[1].is_number() && data[2].is_number()) {
				outValue.value = Engine::Vector3(data[0].get<float>(), data[1].get<float>(), data[2].get<float>());
				return true;
			}
			if (data.size() == 4 && data[0].is_number() && data[1].is_number() && data[2].is_number() && data[3].is_number()) {
				outValue.value = Engine::Vector4(data[0].get<float>(), data[1].get<float>(), data[2].get<float>(), data[3].get<float>());
				return true;
			}
		}
		if (data.is_object()) {
			if (data.contains("r") && data.contains("g") && data.contains("b") && data.contains("a")) {
				outValue.value = Engine::Color4(data.value("r", 0.0f), data.value("g", 0.0f), data.value("b", 0.0f), data.value("a", 1.0f));
				return true;
			}
		}
		return false;
	}

	nlohmann::json SerializeParameterValue(const Engine::MaterialParameterValue& parameter) {

		return std::visit([](const auto& value) -> nlohmann::json {
			using ValueType = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<ValueType, float> ||
				std::is_same_v<ValueType, int32_t> ||
				std::is_same_v<ValueType, uint32_t> ||
				std::is_same_v<ValueType, bool>) {
				return value;
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector2>) {
				return nlohmann::json::array({ value.x, value.y });
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector3>) {
				return nlohmann::json::array({ value.x, value.y, value.z });
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector4>) {
				return nlohmann::json::array({ value.x, value.y, value.z, value.w });
			} else if constexpr (std::is_same_v<ValueType, Engine::Color4>) {
				return nlohmann::json{
					{ "r", value.r },
					{ "g", value.g },
					{ "b", value.b },
					{ "a", value.a },
				};
			} else if constexpr (std::is_same_v<ValueType, Engine::AssetID>) {
				return Engine::ToString(value);
			} else {
				return nlohmann::json{};
			}
			}, parameter.value);
	}
}

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
		pass.materialPathCache = passJson.value("materialPathCache", "");
		pass.passName = passJson.value("passName", "PostProcess");

		if (passJson.contains("parameters") && passJson["parameters"].is_object()) {
			for (auto it = passJson["parameters"].begin(); it != passJson["parameters"].end(); ++it) {
				MaterialParameterValue value{};
				if (TryParseParameterValue(it.value(), value)) {
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
				pass.texturePathCaches[it.key()] = it.value().value("texturePathCache", "");
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
		passJson["materialGuid"] = ToString(pass.materialGuid);
		passJson["materialPathCache"] = pass.materialPathCache;
		passJson["passName"] = pass.passName;

		passJson["parameters"] = nlohmann::json::object();
		for (const auto& [name, value] : pass.parameterOverrides) {
			passJson["parameters"][name] = SerializeParameterValue(value);
		}

		passJson["textures"] = nlohmann::json::object();
		for (const auto& [name, guid] : pass.textureGuids) {
			nlohmann::json texJson = nlohmann::json::object();
			texJson["textureGuid"] = ToString(guid);
			auto cacheIt = pass.texturePathCaches.find(name);
			texJson["texturePathCache"] = (cacheIt != pass.texturePathCaches.end()) ? cacheIt->second : "";
			passJson["textures"][name] = texJson;
		}

		data["passes"].push_back(std::move(passJson));
	}

	return data;
}
