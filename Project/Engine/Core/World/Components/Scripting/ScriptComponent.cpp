#include "ScriptComponent.h"

//============================================================================
//	ScriptComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, ScriptEntry& entry) {

	// 新形式: scriptTypeId（永続主キー）/ scriptSlotID / lastKnownTypeName
	entry.scriptTypeId = in.value("scriptTypeId", std::string{});
	entry.lastKnownTypeName = in.value("lastKnownTypeName", std::string{});

	// legacy 形式: 旧 "type"（クラス名/完全名）を lastKnownTypeName として取り込み、後で GUID へ移行する。
	// 破壊的な上書きはせず、scriptTypeId が空でも serialized fields は保持する。
	if (entry.lastKnownTypeName.empty()) {
		entry.lastKnownTypeName = in.value("type", std::string{});
	}

	// scriptSlotID（無ければ新規発番して同type複数attachを識別できるようにする）
	const UUID parsedSlot = FromString16Hex(in.value("scriptSlotID", std::string{}));
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

	// 永続保存の主キーは Stable Script Type GUID。表示・legacy照合用に lastKnownTypeName も残す
	out["scriptTypeId"] = entry.scriptTypeId;
	out["scriptSlotID"] = ToString(entry.scriptSlotID);
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
		return;
	}
	if (in.is_object() && in.contains("scripts") && in["scripts"].is_array()) {

		component.scripts = in["scripts"].get<std::vector<ScriptEntry>>();
	}
}

void Engine::to_json(nlohmann::json& out, const ScriptComponent& component) {

	out = component.scripts;
}
