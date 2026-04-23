#include "ScriptComponent.h"

//============================================================================
//	ScriptComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, ScriptEntry& entry) {

	entry.type = in.value("type", "");
	entry.scriptAsset = ParseAssetID(in, "scriptAsset");
	entry.enabled = in.value("enabled", true);
	entry.serializedFields = in.value("serializedFields", nlohmann::json::object());
	if (!entry.serializedFields.is_object()) {
		entry.serializedFields = nlohmann::json::object();
	}

	// ランタイムのハンドルはシリアライズされないため、初期化しておく
	entry.handle = BehaviorHandle::Null();
}

void Engine::to_json(nlohmann::json& out, const ScriptEntry& entry) {

	out["type"] = entry.type;
	out["scriptAsset"] = ToString(entry.scriptAsset);
	out["enabled"] = entry.enabled;
	out["serializedFields"] = entry.serializedFields;
}

void Engine::from_json(const nlohmann::json& in, ScriptComponent& component) {

	// クリア
	component.scripts.clear();

	if (in.is_array()) {

		component.scripts = in.get<std::vector<ScriptEntry>>();
		return;
	}
	if (in.is_object() && in.contains("scripts") && in["scripts"].is_array()) {

		component.scripts = in["scripts"].get<std::vector<ScriptEntry>>();
	}
}

void Engine::to_json(nlohmann::json& out, const ScriptComponent& component) {

	out = component.scripts;
}
