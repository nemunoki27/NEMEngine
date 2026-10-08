#include "ScriptFieldStorage.h"

// c++
#include <stdexcept>

nlohmann::json Engine::ScriptFieldStorage::ExtractValues(const nlohmann::json& serializedFields) {

	nlohmann::json result = nlohmann::json::object();
	if (!serializedFields.is_object()) {
		return result;
	}
	const auto fields = serializedFields.find("fields");
	if (fields != serializedFields.end() && fields->is_object()) {
		for (const auto& [id, entry] : fields->items()) {
			result[id] = entry.is_object() && entry.contains("value") ? entry["value"] : entry;
		}
	}
	// 共有参照と循環参照の定義をFieldと一緒に渡す
	if (serializedFields.contains("$managedReferences")) {
		result["$managedReferences"] = serializedFields["$managedReferences"];
	}
	return result;
}

void Engine::ScriptFieldStorage::MergeValues(nlohmann::json& serializedFields, const nlohmann::json& values) {

	if (!values.is_object() || (!serializedFields.is_null() && !serializedFields.is_object())) {
		throw std::invalid_argument("Scriptの保存値はobjectで指定してください");
	}
	nlohmann::json candidate = serializedFields.is_null() ? nlohmann::json::object() : serializedFields;
	if (!candidate.contains("fields")) {
		candidate["fields"] = nlohmann::json::object();
	}
	if (!candidate["fields"].is_object()) {
		throw std::invalid_argument("Scriptのfields形式が不正です");
	}
	for (const auto& [id, value] : values.items()) {
		if (id == "$managedReferences") {
			continue;
		}
		auto& entry = candidate["fields"][id];
		// 名前や型の補助情報を残して値だけを更新する
		if (!entry.is_object() || !entry.contains("value")) {
			entry = nlohmann::json::object();
		}
		entry["value"] = value;
	}
	if (values.contains("$managedReferences")) {
		candidate["$managedReferences"] = values["$managedReferences"];
	} else {
		candidate.erase("$managedReferences");
	}
	serializedFields = std::move(candidate);
}
