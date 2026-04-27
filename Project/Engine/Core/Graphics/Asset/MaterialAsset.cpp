#include "MaterialAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Enum/EnumAdapter.h>

// c++
#include <type_traits>

//============================================================================
//	MaterialAsset classMethods
//============================================================================

namespace {

	// JSONからMaterialParameterValueをパースする関数
	bool TryParseParameterValue(const nlohmann::json& data, Engine::MaterialParameterValue& outValue) {

		if (data.is_number_float()) {
			outValue.value = data.get<float>();
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

	// MaterialParameterValueをJSONへ変換する関数
	nlohmann::json SerializeParameterValue(const Engine::MaterialParameterValue& parameter) {

		return std::visit([](const auto& value) -> nlohmann::json {
			using ValueType = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<ValueType, float> ||
				std::is_same_v<ValueType, int32_t> ||
				std::is_same_v<ValueType, uint32_t>) {

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

bool Engine::FromJson(const nlohmann::json& data, MaterialAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}

	outAsset = MaterialAsset{};
	outAsset.guid = ParseAssetID(data, "guid");
	outAsset.name = data.value("name", "UnnamedMaterial");
	outAsset.domain = EnumAdapter<MaterialDomain>::FromString(data.value("domain", "Surface")).value_or(MaterialDomain::Surface);
	if (data.contains("passes") && data["passes"].is_array()) {
		for (const auto& passJson : data["passes"]) {

			if (!passJson.is_object()) {
				continue;
			}

			MaterialPassBinding binding{};
			binding.passName = passJson.value("passName", "");
			binding.pipeline = ParseAssetID(passJson, "pipeline");
			binding.preferredVariant = EnumAdapter<PipelineVariantKind>::FromString(passJson.value("preferredVariant",
				"GraphicsVertex")).value_or(PipelineVariantKind::GraphicsVertex);

			if (binding.passName.empty() || !binding.pipeline) {
				continue;
			}
			outAsset.passes.emplace_back(std::move(binding));
		}
	}

	if (data.contains("parameters") && data["parameters"].is_object()) {
		for (auto it = data["parameters"].begin(); it != data["parameters"].end(); ++it) {
			MaterialParameterValue value{};
			if (!TryParseParameterValue(it.value(), value)) {
				continue;
			}
			outAsset.parameters[it.key()] = std::move(value);
		}
	}
	return true;
}

nlohmann::json Engine::ToJson(const MaterialAsset& asset) {

	nlohmann::json data = nlohmann::json::object();

	data["guid"] = ToString(asset.guid);
	data["name"] = asset.name;
	data["domain"] = EnumAdapter<MaterialDomain>::ToString(asset.domain);
	data["passes"] = nlohmann::json::array();
	for (const auto& pass : asset.passes) {
		nlohmann::json item = nlohmann::json::object();
		item["passName"] = pass.passName;
		item["pipeline"] = ToString(pass.pipeline);
		item["preferredVariant"] = EnumAdapter<PipelineVariantKind>::ToString(pass.preferredVariant);
		data["passes"].push_back(item);
	}
	data["parameters"] = nlohmann::json::object();
	for (const auto& [name, parameter] : asset.parameters) {
		data["parameters"][name] = SerializeParameterValue(parameter);
	}
	return data;
}

const Engine::MaterialPassBinding* Engine::FindPass(const MaterialAsset& asset, const std::string_view& passName) {

	for (const auto& pass : asset.passes) {
		if (pass.passName == passName) {
			return &pass;
		}
	}
	return nullptr;
}
