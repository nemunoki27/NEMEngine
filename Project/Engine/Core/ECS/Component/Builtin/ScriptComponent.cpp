#include "ScriptComponent.h"

//============================================================================
//	ScriptComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, ScriptEntry& entry) {

	entry.type = in.value("type", "");
	entry.enabled = in.value("enabled", true);

	// ランタイムのハンドルはシリアライズされないため、初期化しておく
	entry.handle = BehaviorHandle::Null();
}

void Engine::to_json(nlohmann::json& out, const ScriptEntry& entry) {

	out["type"] = entry.type;
	out["enabled"] = entry.enabled;
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