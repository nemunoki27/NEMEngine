#include "PrefabReferenceRemapper.h"

//============================================================================
//	include
//============================================================================

// c++
#include <cstdint>
#include <unordered_set>
#include <vector>

//============================================================================
//	PrefabReferenceRemapper internalMethods
//============================================================================
namespace {

	// 参照空間をJSON文字列へ変換する
	const char* ToReferenceKind(Engine::PrefabReferenceRemapper::ReferenceSpace referenceSpace) {

		return referenceSpace == Engine::PrefabReferenceRemapper::ReferenceSpace::Prefab ? "Prefab" : "Scene";
	}

	// JSON値からUUIDを読む
	Engine::UUID ReadUUIDValue(const nlohmann::json& value) {

		Engine::UUID result{};
		if (value.is_string()) {
			return Engine::FromString16Hex(value.get<std::string>());
		}
		if (value.is_number_unsigned()) {
			result.value = value.get<uint64_t>();
		} else if (value.is_number_integer()) {

			const int64_t raw = value.get<int64_t>();
			if (raw > 0) {
				result.value = static_cast<uint64_t>(raw);
			}
		}
		return result;
	}

	// JSON値へUUIDを書き戻す
	void WriteUUIDValue(nlohmann::json& value, Engine::UUID id) {

		if (value.is_number_unsigned() || value.is_number_integer()) {
			value = id.value;
			return;
		}
		value = id ? Engine::ToString(id) : std::string{};
	}

	// 指定キーのUUID値を読む
	Engine::UUID ReadUUIDKey(const nlohmann::json& object, const char* key) {

		if (!object.is_object() || !object.contains(key)) {
			return Engine::UUID{};
		}
		return ReadUUIDValue(object[key]);
	}

	// Prefab実体を識別するローカルIDを読む
	Engine::UUID ReadEntityLocalFileID(const nlohmann::json& entityJson) {

		if (!entityJson.is_object()) {
			return Engine::UUID{};
		}
		if (entityJson.contains("LocalFileID")) {
			return ReadUUIDValue(entityJson["LocalFileID"]);
		}
		if (entityJson.contains("UUID")) {
			return ReadUUIDValue(entityJson["UUID"]);
		}
		return Engine::UUID{};
	}

	// 指定キーのUUID値をリマップする
	void RemapUUIDKey(nlohmann::json& object, const char* key,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& localFileIDMap) {

		if (!object.is_object() || !object.contains(key)) {
			return;
		}
		const Engine::UUID current = ReadUUIDValue(object[key]);
		if (!current) {
			return;
		}
		auto it = localFileIDMap.find(current);
		if (it == localFileIDMap.end() || !it->second) {
			return;
		}
		WriteUUIDValue(object[key], it->second);
	}

	// EntityRef形式か判定する
	bool IsEntityRefObject(const nlohmann::json& value) {

		return value.is_object() && value.contains("kind") &&
			value.contains("sourceAsset") && value.contains("localFileId");
	}

	// EntityRefオブジェクトをリマップする
	void RemapEntityRef(nlohmann::json& value,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& localFileIDMap,
		Engine::PrefabReferenceRemapper::ReferenceSpace referenceSpace, Engine::AssetID sourceAsset) {

		const Engine::UUID current = ReadUUIDKey(value, "localFileId");
		if (!current) {
			return;
		}
		auto it = localFileIDMap.find(current);
		if (it == localFileIDMap.end() || !it->second) {
			return;
		}
		value["kind"] = ToReferenceKind(referenceSpace);
		value["sourceAsset"] = sourceAsset ? Engine::ToString(sourceAsset) : std::string{};
		value["localFileId"] = Engine::ToString(it->second);
	}

	// JSONツリー内のEntity参照を再帰的にリマップする
	void RemapTree(nlohmann::json& value,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& localFileIDMap,
		Engine::PrefabReferenceRemapper::ReferenceSpace referenceSpace, Engine::AssetID sourceAsset) {

		if (value.is_object()) {

			if (IsEntityRefObject(value)) {
				RemapEntityRef(value, localFileIDMap, referenceSpace, sourceAsset);
				return;
			}
			for (auto it = value.begin(); it != value.end(); ++it) {
				RemapTree(it.value(), localFileIDMap, referenceSpace, sourceAsset);
			}
			return;
		}
		if (value.is_array()) {
			for (auto& element : value) {
				RemapTree(element, localFileIDMap, referenceSpace, sourceAsset);
			}
		}
	}

	// カメラ制御設定内のtargetをリマップする
	void RemapCameraTarget(nlohmann::json& component, const char* group,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& localFileIDMap) {

		if (!component.contains(group) || !component[group].is_object()) {
			return;
		}
		RemapUUIDKey(component[group], "target", localFileIDMap);
	}

	// 追従と注視を併用するカメラ設定をリマップする
	void RemapFollowLookAtTarget(nlohmann::json& component,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& localFileIDMap) {

		if (!component.contains("followLookAt") || !component["followLookAt"].is_object()) {
			return;
		}
		RemapCameraTarget(component["followLookAt"], "follow", localFileIDMap);
		RemapCameraTarget(component["followLookAt"], "lookAt", localFileIDMap);
	}

	// エフェクト発生設定内の親エンティティをリマップする
	void RemapEffectEmitterParents(nlohmann::json& component,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& localFileIDMap) {

		if (!component.contains("groups") || !component["groups"].is_array()) {
			return;
		}
		for (nlohmann::json& group : component["groups"]) {

			if (!group.is_object() || !group.contains("states") || !group["states"].is_array()) {
				continue;
			}
			for (nlohmann::json& state : group["states"]) {

				if (!state.is_object() || !state.contains("parentSettings") ||
					!state["parentSettings"].is_object()) {
					continue;
				}
				RemapUUIDKey(state["parentSettings"], "entityLocalFileID", localFileIDMap);
			}
		}
	}

	// コンポーネント固有のローカルIDフィールドをリマップする
	void RemapNativeComponentFields(const std::string& componentType, nlohmann::json& component,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& localFileIDMap) {

		if (!component.is_object()) {
			return;
		}
		if (componentType == "Hierarchy") {
			RemapUUIDKey(component, "parentLocalFileID", localFileIDMap);
		} else if (componentType == "LineRenderer") {
			RemapUUIDKey(component, "parentLocalFileID", localFileIDMap);
		} else if (componentType == "JointAttachment") {
			RemapUUIDKey(component, "skinnedEntityLocalFileID", localFileIDMap);
		} else if (componentType == "CameraController") {
			RemapCameraTarget(component, "follow", localFileIDMap);
			RemapCameraTarget(component, "lookAt", localFileIDMap);
			RemapFollowLookAtTarget(component, localFileIDMap);
		} else if (componentType == "EffectEmitter" || componentType == "ParticleEmitter") {
			RemapEffectEmitterParents(component, localFileIDMap);
		}
	}

	// 差分パスがローカルIDのリーフか判定する
	bool IsLocalFileIDLeafPath(const std::string& path) {

		return path == "Hierarchy/parentLocalFileID" ||
			path == "LineRenderer/parentLocalFileID" ||
			path == "JointAttachment/skinnedEntityLocalFileID" ||
			path == "CameraController/follow/target" ||
			path == "CameraController/lookAt/target" ||
			path == "CameraController/followLookAt/follow/target" ||
			path == "CameraController/followLookAt/lookAt/target" ||
			path.ends_with("/parentSettings/entityLocalFileID") ||
			path.ends_with("/localFileId");
	}

	// 差分リーフのUUID値をリマップする
	void RemapLeafValue(nlohmann::json& value,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& localFileIDMap) {

		const Engine::UUID current = ReadUUIDValue(value);
		if (!current) {
			return;
		}
		auto it = localFileIDMap.find(current);
		if (it == localFileIDMap.end() || !it->second) {
			return;
		}
		WriteUUIDValue(value, it->second);
	}

	// ScriptEntryのslot IDを読み取る
	Engine::UUID ReadScriptSlotID(const nlohmann::json& scriptEntry) {

		if (!scriptEntry.is_object()) {
			return Engine::UUID{};
		}
		if (scriptEntry.contains("scriptSlotId")) {
			return ReadUUIDValue(scriptEntry["scriptSlotId"]);
		}
		return ReadUUIDKey(scriptEntry, "scriptSlotID");
	}

	// ScriptRefのslot IDを読み取る
	Engine::UUID ReadScriptRefSlotID(const nlohmann::json& scriptRef) {

		if (!scriptRef.is_object()) {
			return Engine::UUID{};
		}
		if (scriptRef.contains("scriptSlotId")) {
			return ReadUUIDValue(scriptRef["scriptSlotId"]);
		}
		return ReadUUIDKey(scriptRef, "scriptSlotID");
	}

	// Prefabファイル内のscriptSlotIDからPrefabローカルIDへの表を作る
	Engine::PrefabReferenceRemapper::LocalFileIDMap BuildScriptSlotMap(const nlohmann::json& prefabFileJson) {

		Engine::PrefabReferenceRemapper::LocalFileIDMap result;
		if (!prefabFileJson.is_object() || !prefabFileJson.contains("Entities") ||
			!prefabFileJson["Entities"].is_array()) {
			return result;
		}
		for (const auto& entityJson : prefabFileJson["Entities"]) {

			const Engine::UUID localFileID = ReadUUIDKey(entityJson, "LocalFileID");
			if (!localFileID || !entityJson.contains("Components") || !entityJson["Components"].is_object()) {
				continue;
			}
			const nlohmann::json& components = entityJson["Components"];
			if (!components.contains("Script") || !components["Script"].is_array()) {
				continue;
			}
			for (const auto& scriptEntry : components["Script"]) {

				const Engine::UUID slotID = ReadScriptSlotID(scriptEntry);
				if (slotID) {
					result.emplace(slotID, localFileID);
				}
			}
		}
		return result;
	}

	// ScriptRefのentityをslot IDから復旧する
	void RepairScriptRefTree(nlohmann::json& value,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& scriptSlotToLocalFileID,
		Engine::AssetID prefabAsset) {

		if (value.is_object()) {

			if (value.contains("entity") && value["entity"].is_object() &&
				(value.contains("scriptSlotId") || value.contains("scriptSlotID"))) {

				const Engine::UUID slotID = ReadScriptRefSlotID(value);
				auto it = scriptSlotToLocalFileID.find(slotID);
				if (it != scriptSlotToLocalFileID.end() && it->second) {

					nlohmann::json& entity = value["entity"];
					entity["kind"] = "Prefab";
					entity["sourceAsset"] = prefabAsset ? Engine::ToString(prefabAsset) : std::string{};
					entity["localFileId"] = Engine::ToString(it->second);
				}
			}
			for (auto it = value.begin(); it != value.end(); ++it) {
				RepairScriptRefTree(it.value(), scriptSlotToLocalFileID, prefabAsset);
			}
			return;
		}
		if (value.is_array()) {
			for (auto& element : value) {
				RepairScriptRefTree(element, scriptSlotToLocalFileID, prefabAsset);
			}
		}
	}
}

//============================================================================
//	PrefabReferenceRemapper classMethods
//============================================================================
void Engine::PrefabReferenceRemapper::RemapComponents(nlohmann::json& components,
	const LocalFileIDMap& localFileIDMap, ReferenceSpace referenceSpace, AssetID sourceAsset) {

	if (!components.is_object() || localFileIDMap.empty()) {
		return;
	}
	for (auto it = components.begin(); it != components.end(); ++it) {
		RemapComponent(it.key(), it.value(), localFileIDMap, referenceSpace, sourceAsset);
	}
}

void Engine::PrefabReferenceRemapper::RemapComponent(const std::string& componentType, nlohmann::json& component,
	const LocalFileIDMap& localFileIDMap, ReferenceSpace referenceSpace, AssetID sourceAsset) {

	if (localFileIDMap.empty()) {
		return;
	}
	RemapNativeComponentFields(componentType, component, localFileIDMap);
	RemapTree(component, localFileIDMap, referenceSpace, sourceAsset);
}

void Engine::PrefabReferenceRemapper::RemapValue(nlohmann::json& value, const std::string& path,
	const LocalFileIDMap& localFileIDMap, ReferenceSpace referenceSpace, AssetID sourceAsset) {

	if (localFileIDMap.empty()) {
		return;
	}
	if (IsLocalFileIDLeafPath(path)) {
		RemapLeafValue(value, localFileIDMap);
		return;
	}
	RemapTree(value, localFileIDMap, referenceSpace, sourceAsset);
}

void Engine::PrefabReferenceRemapper::NormalizePrefabFileJointAttachments(nlohmann::json& prefabFileJson) {

	if (!prefabFileJson.is_object() || !prefabFileJson.contains("Entities") ||
		!prefabFileJson["Entities"].is_array()) {
		return;
	}

	UUID rootLocalFileID{};
	if (prefabFileJson.contains("Header") && prefabFileJson["Header"].is_object()) {
		rootLocalFileID = ReadUUIDKey(prefabFileJson["Header"], "rootLocalFileID");
	}

	std::unordered_set<UUID> entityLocalFileIDs;
	for (const auto& entityJson : prefabFileJson["Entities"]) {

		const UUID localFileID = ReadEntityLocalFileID(entityJson);
		if (localFileID) {
			entityLocalFileIDs.insert(localFileID);
		}
	}

	for (auto& entityJson : prefabFileJson["Entities"]) {

		const UUID localFileID = ReadEntityLocalFileID(entityJson);
		if (!localFileID || !entityJson.contains("Components") || !entityJson["Components"].is_object()) {
			continue;
		}
		auto& components = entityJson["Components"];
		if (!components.contains("JointAttachment") || !components["JointAttachment"].is_object()) {
			continue;
		}
		const UUID targetLocalFileID =
			ReadUUIDKey(components["JointAttachment"], "skinnedEntityLocalFileID");
		if (localFileID == rootLocalFileID || !targetLocalFileID ||
			!entityLocalFileIDs.contains(targetLocalFileID)) {
			components.erase("JointAttachment");
			continue;
		}
		if (!components.contains("Hierarchy") || !components["Hierarchy"].is_object()) {
			continue;
		}
		auto& parentLocalFileID = components["Hierarchy"]["parentLocalFileID"];
		WriteUUIDValue(parentLocalFileID, UUID{});
	}
}

void Engine::PrefabReferenceRemapper::RepairPrefabFileScriptRefs(nlohmann::json& prefabFileJson, AssetID prefabAsset) {

	const LocalFileIDMap scriptSlotToLocalFileID = BuildScriptSlotMap(prefabFileJson);
	if (scriptSlotToLocalFileID.empty()) {
		return;
	}
	RepairScriptRefTree(prefabFileJson, scriptSlotToLocalFileID, prefabAsset);
}
