#include "PrefabReferenceRemapper.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/World/Prefab/Override/PrefabJsonDiff.h>

// c++
#include <cstdint>
#include <functional>
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

	// JSONツリー内に残ったScene参照を空にする
	void ClearSceneReferenceTree(nlohmann::json& value) {

		if (value.is_object()) {

			if (IsEntityRefObject(value)) {
				if (value.value("kind", std::string{}) == "Scene") {
					value["kind"] = "Null";
					value["sourceAsset"] = std::string{};
					value["localFileId"] = std::string{};
				}
				return;
			}
			for (auto it = value.begin(); it != value.end(); ++it) {
				ClearSceneReferenceTree(it.value());
			}
			return;
		}
		if (value.is_array()) {
			for (auto& element : value) {
				ClearSceneReferenceTree(element);
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
bool Engine::PrefabReferenceRemapper::NormalizeLegacySceneInstances(
	nlohmann::json& scene, AssetID sourceAsset, std::string& diagnostic, AssetDatabase* database) {

	diagnostic.clear();
	if (!scene.is_object() || !scene.contains("PrefabInstances")) {
		return true;
	}
	// 失敗時は入力を保持し、実体の作成前に全シーンの競合を検査する
	auto candidate = scene;
	LocalFileIDMap aliases;
	std::unordered_set<UUID> sceneIDs;
	std::unordered_set<UUID> instanceIDs;
	const auto reserveID = [&](UUID id) {
		if (!id || !sceneIDs.insert(id).second) {
			diagnostic = "シーンIDが不正または別の実体と競合しています ID=" + ToString(id);
			return false;
		}
		return true;
	};
	try {
		for (const auto& entity : candidate.value("Entities", nlohmann::json::array())) {
			if (!reserveID(ReadUUIDKey(entity, "LocalFileID"))) return false;
		}
		std::function<bool(nlohmann::json&, uint32_t)> normalize;
		normalize = [&](nlohmann::json& instance, uint32_t depth) {
			if (depth >= 32 || !instance.is_object() || !instance.contains("EntityMap") ||
				!instance["EntityMap"].is_array()) {
				diagnostic = "Prefabの対応表またはネスト階層が不正です";
				return false;
			}
			LocalFileIDMap firstIDs;
			const UUID instanceID = ReadUUIDKey(instance, "InstanceID");
			if (!instanceID || !instanceIDs.insert(instanceID).second) {
				diagnostic = "PrefabインスタンスIDが不正または重複しています ID=" + ToString(instanceID);
				return false;
			}
			nlohmann::json pairs = nlohmann::json::array();
			for (const auto& pair : instance["EntityMap"]) {
				const UUID prefabID = ReadUUIDKey(pair, "P");
				const UUID sceneID = ReadUUIDKey(pair, "S");
				if (!prefabID) {
					diagnostic = "Prefab内IDが不正です InstanceID=" + ToString(instanceID);
					return false;
				}
				if (!reserveID(sceneID)) return false;
				const auto [first, inserted] = firstIDs.emplace(prefabID, sceneID);
				if (inserted) {
					pairs.push_back(pair);
				} else {
					aliases.emplace(sceneID, first->second);
					diagnostic += "InstanceID=" + instance.value("InstanceID", "") +
						" PrefabID=" + ToString(prefabID) + " " + ToString(sceneID) +
						" -> " + ToString(first->second) + "\n";
				}
			}
			instance["EntityMap"] = std::move(pairs);
			for (const auto& added : instance.value("AddedEntities", nlohmann::json::array())) {
				if (!reserveID(ReadUUIDKey(added, "SceneLocalFileID"))) return false;
			}
			if (instance.contains("NestedInstances")) {
				if (!instance["NestedInstances"].is_array()) return false;
				for (auto& nested : instance["NestedInstances"]) {
					if (!normalize(nested, depth + 1)) return false;
				}
			}
			return true;
		};
		if (!candidate["PrefabInstances"].is_array()) return false;
		for (auto& instance : candidate["PrefabInstances"]) {
			if (!normalize(instance, 0)) return false;
		}
		if (aliases.empty()) return true;

		// 他アセットやPrefab空間の同名IDには触れない
		std::function<void(nlohmann::json&)> remapReferences;
		remapReferences = [&](nlohmann::json& value) {
			if (IsEntityRefObject(value)) {
				const auto asset = value.value("sourceAsset", "");
				if (value.value("kind", "") == "Scene" &&
					(asset.empty() || asset == ToString(sourceAsset))) {
					RemapUUIDKey(value, "localFileId", aliases);
				}
				return;
			}
			if (value.is_object() || value.is_array()) {
				for (auto& child : value) remapReferences(child);
			}
		};
		const auto remapComponents = [&](nlohmann::json& components) {
			if (!components.is_object()) return;
			for (auto it = components.begin(); it != components.end(); ++it) {
				RemapNativeComponentFields(it.key(), it.value(), aliases);
			}
			remapReferences(components);
		};
		bool referencesValid = true;
		std::function<void(nlohmann::json&)> remapInstance;
		remapInstance = [&](nlohmann::json& instance) {
			RemapUUIDKey(instance, "RootParent", aliases);
			for (auto& added : instance["AddedEntities"]) {
				RemapUUIDKey(added, "Parent", aliases);
				remapComponents(added["Components"]);
			}
			for (auto& mod : instance["HierarchyMods"]) RemapUUIDKey(mod, "ExternalParent", aliases);
			for (auto& mod : instance["AddedComponents"]) {
				RemapNativeComponentFields(mod.value("Type", ""), mod["Value"], aliases);
				remapReferences(mod["Value"]);
			}
			for (auto& mod : instance["Modifications"]) {
				const auto path = mod.value("Path", "");
				if (path.ends_with("/localFileId") && aliases.contains(ReadUUIDValue(mod["Value"]))) {
					// リーフ差分の参照空間は元コンポーネントと同じ対象の差分から復元する
					nlohmann::json components = nlohmann::json::object();
					if (database) {
						const auto asset = ParseAssetID(instance, "PrefabAsset");
						const auto file = JsonAdapter::Load(database->ResolveFullPath(asset), false);
						if (file.contains("Entities") && file["Entities"].is_array()) {
							for (const auto& entity : file["Entities"]) {
								if (entity.value("LocalFileID", "") == mod.value("Target", "")) {
									components = entity.value("Components", nlohmann::json::object());
									break;
								}
							}
						}
					}
					for (const auto& field : instance["Modifications"]) {
						if (field.value("Target", "") == mod.value("Target", "")) {
							PrefabJsonDiff::SetAtPath(components, field.value("Path", ""), field["Value"]);
						}
					}
					const auto* reference = PrefabJsonDiff::GetAtPath(components, path.substr(0, path.rfind('/')));
					if (!reference || !IsEntityRefObject(*reference)) {
						diagnostic = "差分の参照空間を復元できません InstanceID=" + instance.value("InstanceID", "") +
							" Path=" + path;
						referencesValid = false;
					} else if (reference->value("kind", "") == "Scene" &&
						(reference->value("sourceAsset", "").empty() || reference->value("sourceAsset", "") == ToString(sourceAsset))) {
						RemapLeafValue(mod["Value"], aliases);
					}
				}
				if (IsLocalFileIDLeafPath(path) && !path.ends_with("/localFileId")) {
					RemapLeafValue(mod["Value"], aliases);
				}
				if (path.find('/') == std::string::npos) RemapNativeComponentFields(path, mod["Value"], aliases);
				remapReferences(mod["Value"]);
			}
			for (auto& nested : instance["NestedInstances"]) remapInstance(nested);
		};
		for (auto& entity : candidate["Entities"]) remapComponents(entity["Components"]);
		for (auto& instance : candidate["PrefabInstances"]) remapInstance(instance);
		if (!referencesValid) return false;
	} catch (const nlohmann::json::exception& error) {
		diagnostic = "Prefab復旧データの解析に失敗しました: " + std::string(error.what());
		return false;
	}
	scene = std::move(candidate);
	diagnostic = "統合件数=" + std::to_string(aliases.size()) + "\n" + diagnostic;
	return true;
}

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

void Engine::PrefabReferenceRemapper::ClearExternalSceneReferences(nlohmann::json& value) {

	ClearSceneReferenceTree(value);
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
