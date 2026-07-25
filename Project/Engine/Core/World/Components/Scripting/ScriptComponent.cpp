#include "ScriptComponent.h"

//============================================================================
//	ScriptComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, ScriptEntry& entry) {

	// scriptTypeIDを永続主キーとして読み込む
	entry.scriptTypeID = in.value("scriptTypeId", std::string{});
	entry.lastKnownTypeName = in.value("lastKnownTypeName", std::string{});

	const std::string slotText = in.value("scriptSlotId", std::string{});
	const UUID parsedSlot = FromString16Hex(slotText);
	entry.scriptSlotID = parsedSlot ? parsedSlot : UUID::New();

	entry.scriptAsset = ParseAssetID(in, "scriptAsset");
	entry.enabled = in.value("enabled", true);
	entry.serializedFields = in.value("serializedFields", nlohmann::json::object());
	if (!entry.serializedFields.is_object()) {
		entry.serializedFields = nlohmann::json::object();
	}

	// ランタイムキャッシュはシリアライズされないため、初期化しておく
	entry.handle = BehaviorHandle::Null();
	entry.resolvedRuntimeTypeID = 0;
	entry.resolvedRuntimeTypeValid = false;
	entry.serializedRevision = 0;
}

void Engine::to_json(nlohmann::json& out, const ScriptEntry& entry) {

	// 型名はInspector表示とMissing Script診断に使う
	out["scriptTypeId"] = entry.scriptTypeID;
	out["scriptSlotId"] = ToString(entry.scriptSlotID);
	out["lastKnownTypeName"] = entry.lastKnownTypeName;
	out["scriptAsset"] = ToAssetReferenceJson(entry.scriptAsset);
	out["enabled"] = entry.enabled;
	out["serializedFields"] = entry.serializedFields;
}

void Engine::from_json(const nlohmann::json& in, ScriptComponent& component) {

	// クリア
	component.scripts.clear();

	if (in.is_array()) {
		component.scripts = in.get<std::vector<ScriptEntry>>();
	}
}

void Engine::to_json(nlohmann::json& out, const ScriptComponent& component) {

	out = component.scripts;
}
