#include "PrefabReferenceRemapper.h"
#include "PrefabReferenceFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/World/Prefab/Override/PrefabJsonDiff.h>

// c++
#include <functional>
#include <unordered_set>
#include <utility>

using namespace Engine::PrefabReferenceFields;

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
			if (!reserveID(ReadUUIDKey(entity, "LocalFileID"))) {
				return false;
			}
		}
		std::function<bool(nlohmann::json&, uint32_t)> normalize;
		normalize = [&](nlohmann::json& instance, uint32_t depth) {
			if (depth >= 32 || !instance.is_object() || !instance.contains("EntityMap") || !instance["EntityMap"].is_array()) {
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
				if (!reserveID(sceneID)) {
					return false;
				}
				const auto [first, inserted] = firstIDs.emplace(prefabID, sceneID);
				if (inserted) {
					pairs.push_back(pair);
				} else {
					aliases.emplace(sceneID, first->second);
					diagnostic += "InstanceID=" + instance.value("InstanceID", "") + " PrefabID=" + ToString(prefabID) + " " +
								  ToString(sceneID) + " -> " + ToString(first->second) + "\n";
				}
			}
			instance["EntityMap"] = std::move(pairs);
			for (const auto& added : instance.value("AddedEntities", nlohmann::json::array())) {
				if (!reserveID(ReadUUIDKey(added, "SceneLocalFileID"))) {
					return false;
				}
			}
			if (instance.contains("NestedInstances")) {
				if (!instance["NestedInstances"].is_array()) {
					return false;
				}
				for (auto& nested : instance["NestedInstances"]) {
					if (!normalize(nested, depth + 1)) {
						return false;
					}
				}
			}
			return true;
		};
		if (!candidate["PrefabInstances"].is_array()) {
			return false;
		}
		for (auto& instance : candidate["PrefabInstances"]) {
			if (!normalize(instance, 0)) {
				return false;
			}
		}
		if (aliases.empty()) {
			return true;
		}

		// 他アセットやPrefab空間の同名IDには触れない
		std::function<void(nlohmann::json&)> remapReferences;
		remapReferences = [&](nlohmann::json& value) {
			if (IsEntityRefObject(value)) {
				const auto asset = value.value("sourceAsset", "");
				if (value.value("kind", "") == "Scene" && (asset.empty() || asset == ToString(sourceAsset))) {
					RemapUUIDKey(value, "localFileId", aliases);
				}
				return;
			}
			if (value.is_object() || value.is_array()) {
				for (auto& child : value) {
					remapReferences(child);
				}
			}
		};
		const auto remapComponents = [&](nlohmann::json& components) {
			if (!components.is_object()) {
				return;
			}
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
			for (auto& mod : instance["HierarchyMods"]) {
				RemapUUIDKey(mod, "ExternalParent", aliases);
			}
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
						diagnostic =
							"差分の参照空間を復元できません InstanceID=" + instance.value("InstanceID", "") + " Path=" + path;
						referencesValid = false;
					} else if (reference->value("kind", "") == "Scene" &&
							   (reference->value("sourceAsset", "").empty() ||
								   reference->value("sourceAsset", "") == ToString(sourceAsset))) {
						RemapLeafValue(mod["Value"], aliases);
					}
				}
				if (IsLocalFileIDLeafPath(path) && !path.ends_with("/localFileId")) {
					RemapLeafValue(mod["Value"], aliases);
				}
				if (path.find('/') == std::string::npos) {
					RemapNativeComponentFields(path, mod["Value"], aliases);
				}
				remapReferences(mod["Value"]);
			}
			for (auto& nested : instance["NestedInstances"]) {
				remapInstance(nested);
			}
		};
		for (auto& entity : candidate["Entities"]) {
			remapComponents(entity["Components"]);
		}
		for (auto& instance : candidate["PrefabInstances"]) {
			remapInstance(instance);
		}
		if (!referencesValid) {
			return false;
		}
	} catch (const nlohmann::json::exception& error) {
		diagnostic = "Prefab復旧データの解析に失敗しました: " + std::string(error.what());
		return false;
	}
	scene = std::move(candidate);
	diagnostic = "統合件数=" + std::to_string(aliases.size()) + "\n" + diagnostic;
	return true;
}
