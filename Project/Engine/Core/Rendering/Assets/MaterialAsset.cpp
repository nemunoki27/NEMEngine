#include "MaterialAsset.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

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

//============================================================================
//	MaterialParameterOverrides classMethods
//============================================================================
Engine::MaterialParameterOverrides::MaterialParameterOverrides(
	const MaterialParameterOverrides& other) {

	if (!other.empty()) {
		values_ = std::make_unique<Map>(other.Get());
	}
}

Engine::MaterialParameterOverrides&
Engine::MaterialParameterOverrides::operator=(
	const MaterialParameterOverrides& other) {

	if (this == &other) {
		return *this;
	}
	if (other.empty()) {
		values_.reset();
	} else {
		values_ = std::make_unique<Map>(other.Get());
	}
	return *this;
}

Engine::MaterialParameterValue&
Engine::MaterialParameterOverrides::operator[](const std::string& name) {

	return GetMutable()[name];
}

Engine::MaterialParameterValue&
Engine::MaterialParameterOverrides::operator[](const char* name) {

	return GetMutable()[name];
}

void Engine::MaterialParameterOverrides::clear() {

	values_.reset();
}

size_t Engine::MaterialParameterOverrides::erase(const std::string& name) {

	if (!values_) {
		return 0;
	}
	const size_t erased = values_->erase(name);
	if (values_->empty()) {
		values_.reset();
	}
	return erased;
}

Engine::MaterialParameterOverrides::iterator
Engine::MaterialParameterOverrides::erase(iterator position) {

	return GetMutable().erase(position);
}

size_t Engine::MaterialParameterOverrides::count(
	const std::string& name) const {

	return values_ ? values_->count(name) : 0;
}

bool Engine::MaterialParameterOverrides::contains(
	const std::string& name) const {

	return values_ && values_->contains(name);
}

Engine::MaterialParameterOverrides::iterator
Engine::MaterialParameterOverrides::begin() {

	return GetMutable().begin();
}

Engine::MaterialParameterOverrides::iterator
Engine::MaterialParameterOverrides::end() {

	return GetMutable().end();
}

Engine::MaterialParameterOverrides::const_iterator
Engine::MaterialParameterOverrides::begin() const {

	return Get().begin();
}

Engine::MaterialParameterOverrides::const_iterator
Engine::MaterialParameterOverrides::end() const {

	return Get().end();
}

Engine::MaterialParameterOverrides::iterator
Engine::MaterialParameterOverrides::find(const std::string& name) {

	return GetMutable().find(name);
}

Engine::MaterialParameterOverrides::const_iterator
Engine::MaterialParameterOverrides::find(const std::string& name) const {

	return Get().find(name);
}

Engine::MaterialParameterOverrides::Map&
Engine::MaterialParameterOverrides::GetMutable() {

	if (!values_) {
		values_ = std::make_unique<Map>();
	}
	return *values_;
}

const Engine::MaterialParameterOverrides::Map&
Engine::MaterialParameterOverrides::Get() const {

	static const Map empty{};
	return values_ ? *values_ : empty;
}

void Engine::ReadMaterialParameterOverrides(const nlohmann::json& in,
	std::unordered_map<std::string, MaterialParameterValue>& outOverrides) {

	outOverrides.clear();
	if (!in.is_object()) {
		return;
	}
	for (auto it = in.begin(); it != in.end(); ++it) {

		MaterialParameterValue value{};
		if (ParseMaterialParameterValue(it.value(), value)) {
			outOverrides[it.key()] = std::move(value);
		}
	}
}

void Engine::ReadMaterialParameterOverrides(const nlohmann::json& in,
	MaterialParameterOverrides& outOverrides) {

	ReadMaterialParameterOverrides(in, outOverrides.GetMutable());
	if (outOverrides.Get().empty()) {
		outOverrides.clear();
	}
}

nlohmann::json Engine::WriteMaterialParameterOverrides(
	const std::unordered_map<std::string, MaterialParameterValue>& overrides) {

	nlohmann::json out = nlohmann::json::object();
	for (const auto& [name, value] : overrides) {
		out[name] = SerializeMaterialParameterValue(value);
	}
	return out;
}

nlohmann::json Engine::WriteMaterialParameterOverrides(
	const MaterialParameterOverrides& overrides) {

	return WriteMaterialParameterOverrides(overrides.Get());
}

bool Engine::FromJson(const nlohmann::json& data, MaterialAsset& outAsset) {

	if (!data.is_object()) {
		return false;
	}

	outAsset = MaterialAsset{};
	outAsset.name = data.value("name", "UnnamedMaterial");
	outAsset.domain = EnumAdapter<MaterialDomain>::FromString(data.value("domain", "Surface")).value_or(MaterialDomain::Surface);
	outAsset.usage = EnumAdapter<MaterialUsage>::FromString(data.value("usage", "Generic")).value_or(MaterialUsage::Generic);
	if (data.contains("passes") && data["passes"].is_array()) {
		for (const auto& passJson : data["passes"]) {

			if (!passJson.is_object()) {
				continue;
			}

			MaterialPassBinding binding{};
			const auto passKind = EnumAdapter<MaterialPassKind>::FromString(passJson.value("passKind", ""));
			if (!passKind || *passKind == MaterialPassKind::Invalid) {
				continue;
			}
			binding.passKind = *passKind;
			binding.pipeline = ParseAssetID(passJson, "pipeline");
			binding.shaderOverride = ParseAssetID(passJson, "shaderOverride");
			binding.preferredVariant = EnumAdapter<PipelineVariantKind>::FromString(passJson.value("preferredVariant",
				"GraphicsVertex")).value_or(PipelineVariantKind::GraphicsVertex);

			if (!binding.pipeline) {
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

	data["name"] = asset.name;
	data["domain"] = EnumAdapter<MaterialDomain>::ToString(asset.domain);
	data["usage"] = EnumAdapter<MaterialUsage>::ToString(asset.usage);
	data["passes"] = nlohmann::json::array();
	for (const auto& pass : asset.passes) {
		nlohmann::json item = nlohmann::json::object();
		item["passKind"] = EnumAdapter<MaterialPassKind>::ToString(pass.passKind);
		item["pipeline"] = ToAssetReferenceJson(pass.pipeline);
		if (pass.shaderOverride) {
			item["shaderOverride"] = ToAssetReferenceJson(pass.shaderOverride);
		}
		item["preferredVariant"] = EnumAdapter<PipelineVariantKind>::ToString(pass.preferredVariant);
		data["passes"].push_back(item);
	}
	data["parameters"] = nlohmann::json::object();
	for (const auto& [name, parameter] : asset.parameters) {
		data["parameters"][name] = SerializeParameterValue(parameter);
	}
	return data;
}

const Engine::MaterialPassBinding* Engine::FindPass(const MaterialAsset& asset, MaterialPassKind passKind) {

	for (const auto& pass : asset.passes) {
		if (pass.passKind == passKind) {
			return &pass;
		}
	}
	return nullptr;
}
