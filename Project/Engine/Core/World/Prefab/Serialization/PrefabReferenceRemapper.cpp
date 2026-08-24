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

		if (!value.is_string()) {
			return Engine::UUID{};
		}
		return Engine::FromString16Hex(value.get<std::string>());
	}

	// JSON値へUUIDを書き戻す
	void WriteUUIDValue(nlohmann::json& value, Engine::UUID id) {

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
		if (!entityJson.contains("LocalFileID")) {
			return Engine::UUID{};
		}
		return ReadUUIDValue(entityJson["LocalFileID"]);
	}

	// PrefabルートのローカルIDを読む
	Engine::UUID ReadPrefabRootLocalFileID(const nlohmann::json& prefabFileJson) {

		if (!prefabFileJson.contains("Header") || !prefabFileJson["Header"].is_object()) {
			return Engine::UUID{};
		}
		return ReadUUIDKey(prefabFileJson["Header"], "rootLocalFileID");
	}

	// Prefab内に存在する全ローカルIDを集める
	std::unordered_set<Engine::UUID> CollectPrefabEntityLocalFileIDs(const nlohmann::json& prefabFileJson) {

		std::unordered_set<Engine::UUID> result;
		for (const auto& entityJson : prefabFileJson["Entities"]) {

			const Engine::UUID localFileID = ReadEntityLocalFileID(entityJson);
			if (localFileID) {
				result.insert(localFileID);
			}
		}
		return result;
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
		} else if (componentType == "ParticleSystem") {
			RemapUUIDKey(component, "customSimulationTarget", localFileIDMap);
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
			path == "ParticleSystem/customSimulationTarget" ||
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

void Engine::PrefabReferenceRemapper::NormalizePrefabFileHierarchy(nlohmann::json& prefabFileJson) {

	if (!prefabFileJson.is_object() || !prefabFileJson.contains("Entities") ||
		!prefabFileJson["Entities"].is_array()) {
		return;
	}

	const UUID rootLocalFileID = ReadPrefabRootLocalFileID(prefabFileJson);
	const std::unordered_set<UUID> entityLocalFileIDs = CollectPrefabEntityLocalFileIDs(prefabFileJson);

	for (auto& entityJson : prefabFileJson["Entities"]) {

		const UUID localFileID = ReadEntityLocalFileID(entityJson);
		if (!localFileID || !entityJson.contains("Components") || !entityJson["Components"].is_object()) {
			continue;
		}
		auto& components = entityJson["Components"];
		if (!components.contains("Hierarchy") || !components["Hierarchy"].is_object()) {
			continue;
		}
		auto& hierarchy = components["Hierarchy"];
		const UUID parentLocalFileID = ReadUUIDKey(hierarchy, "parentLocalFileID");
		if (localFileID == rootLocalFileID || parentLocalFileID == localFileID ||
			(parentLocalFileID && !entityLocalFileIDs.contains(parentLocalFileID))) {
			WriteUUIDValue(hierarchy["parentLocalFileID"], UUID{});
		}
	}
}

void Engine::PrefabReferenceRemapper::NormalizePrefabFileJointAttachments(nlohmann::json& prefabFileJson) {

	if (!prefabFileJson.is_object() || !prefabFileJson.contains("Entities") ||
		!prefabFileJson["Entities"].is_array()) {
		return;
	}

	const UUID rootLocalFileID = ReadPrefabRootLocalFileID(prefabFileJson);
	const std::unordered_set<UUID> entityLocalFileIDs = CollectPrefabEntityLocalFileIDs(prefabFileJson);

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
