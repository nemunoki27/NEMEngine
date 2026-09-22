#include <Engine/Core/Assets/RenderComponentTypes.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

#include <type_traits>

namespace {

	// JSONからMaterialParameterValueをパースする関数
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
			// 未設定のアセット参照も型を維持する
			if (text.empty()) {
				outValue.value = Engine::AssetID{};
				return true;
			}
			if (text.size() == 32) {
				outValue.value = Engine::FromString32Hex(text);
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

				return Engine::ToAssetReferenceJson(value);
			} else {

				return nlohmann::json{};
			}
			}, parameter.value);
	}
}

bool Engine::ParseMaterialParameterValue(const nlohmann::json& data, MaterialParameterValue& outValue) {

	return TryParseParameterValue(data, outValue);
}

nlohmann::json Engine::SerializeMaterialParameterValue(const MaterialParameterValue& parameter) {

	return SerializeParameterValue(parameter);
}

void Engine::ReadMaterialInstance(const nlohmann::json& in,
	MaterialInstanceParameters& outOverrides) {

	outOverrides.clear();
	if (in.is_array()) {
		for (const nlohmann::json& record : in) {
			if (!record.is_object()) {
				continue;
			}

			const std::string name = record.value("name", "");
			const UUID parsedID =
				FromString16Hex(record.value("id", ""));
			if (name.empty()) {
				continue;
			}

			MaterialParameterValue value{};
			if (!record.contains("value") ||
				!ParseMaterialParameterValue(record["value"], value)) {

				continue;
			}
			const MaterialParameterSemantic semantic =
				EnumAdapter<MaterialParameterSemantic>::FromString(
					record.value("semantic", "None")).
				value_or(ResolveMaterialParameterSemantic(name));
			outOverrides.Set(
				parsedID ? MaterialParameterID::FromUUID(parsedID) :
				MaterialParameterID::FromName(name),
				name, semantic, value);
		}
		return;
	}
	if (!in.is_object()) {
		return;
	}

	// 旧object形式は読み込み時だけ受け、次回保存でID付きrecordへ移行する
	for (auto it = in.begin(); it != in.end(); ++it) {

		MaterialParameterValue value{};
		if (ParseMaterialParameterValue(it.value(), value)) {
			outOverrides.Set(it.key(), value);
		}
	}
}

nlohmann::json Engine::WriteMaterialInstance(
	const MaterialInstanceParameters& overrides) {

	nlohmann::json out = nlohmann::json::array();
	for (const MaterialParameterRecord& record : overrides.GetRecords()) {
		out.push_back({
			{ "id", ToString(UUID{ record.id.value }) },
			{ "name", record.namedValue.first },
			{ "semantic", EnumAdapter<MaterialParameterSemantic>::ToString(record.semantic) },
			{ "value", SerializeMaterialParameterValue(record.namedValue.second) },
			});
	}
	return out;
}
