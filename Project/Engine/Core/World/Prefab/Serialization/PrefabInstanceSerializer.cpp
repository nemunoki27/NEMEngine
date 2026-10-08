//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <unordered_set>

//============================================================================
//	PrefabInstanceData json変換
//============================================================================
namespace {

	// UUIDを文字列へ、空なら空文字列にする
	std::string UUIDToStringOrEmpty(Engine::UUID id) {
		return id ? Engine::ToString(id) : std::string{};
	}
	// AssetGUIDを文字列へ、空なら空文字列にする
	std::string AssetGUIDToStringOrEmpty(Engine::AssetID id) {
		return id ? Engine::ToString(id) : std::string{};
	}
	// 文字列をUUIDへ、空ならゼロにする
	Engine::UUID StringToUUIDOrZero(const std::string& str) {
		return str.empty() ? Engine::UUID{} : Engine::FromString16Hex(str);
	}
	// 文字列をAssetGUIDへ、空ならゼロにする
	Engine::AssetID StringToAssetGUIDOrZero(const std::string& str) {
		return str.empty() ? Engine::AssetID{} : Engine::FromString32Hex(str);
	}
}

nlohmann::json Engine::ToJson(const PrefabInstanceData& data) {

	PrefabInstanceData canonical = data;
	std::sort(canonical.entityMap.begin(), canonical.entityMap.end(), [](const auto& lhs, const auto& rhs) {
		return lhs.first.value != rhs.first.value ?
			lhs.first.value < rhs.first.value : lhs.second.value < rhs.second.value;
		});
	std::sort(canonical.modifications.begin(), canonical.modifications.end(), [](const auto& lhs, const auto& rhs) {
		return lhs.target.value != rhs.target.value ?
			lhs.target.value < rhs.target.value : lhs.path < rhs.path;
		});
	const auto componentLess = [](const PrefabComponentModification& lhs,
		const PrefabComponentModification& rhs) {
		return lhs.target.value != rhs.target.value ?
			lhs.target.value < rhs.target.value : lhs.type < rhs.type;
		};
	std::sort(canonical.addedComponents.begin(), canonical.addedComponents.end(), componentLess);
	std::sort(canonical.removedComponents.begin(), canonical.removedComponents.end(), componentLess);
	std::sort(canonical.hierarchyModifications.begin(), canonical.hierarchyModifications.end(),
		[](const auto& lhs, const auto& rhs) { return lhs.target.value < rhs.target.value; });
	std::sort(canonical.removedEntities.begin(), canonical.removedEntities.end(),
		[](UUID lhs, UUID rhs) { return lhs.value < rhs.value; });
	std::sort(canonical.addedEntities.begin(), canonical.addedEntities.end(),
		[](const auto& lhs, const auto& rhs) {
		return lhs.sceneLocalFileID.value < rhs.sceneLocalFileID.value;
		});
	std::sort(canonical.nestedInstances.begin(), canonical.nestedInstances.end(),
		[](const auto& lhs, const auto& rhs) {
		return lhs.nestedSlotID.value < rhs.nestedSlotID.value;
		});
	std::sort(canonical.removedNestedSlots.begin(), canonical.removedNestedSlots.end(),
		[](UUID lhs, UUID rhs) { return lhs.value < rhs.value; });

	nlohmann::json json = nlohmann::json::object();
	json["PrefabAsset"] = AssetGUIDToStringOrEmpty(canonical.prefabAsset);
	json["InstanceID"] = UUIDToStringOrEmpty(canonical.instanceID);
	json["RootParent"] = UUIDToStringOrEmpty(canonical.rootParentSceneLocalFileID);
	json["OwnerPrefabInstanceID"] = UUIDToStringOrEmpty(canonical.ownerPrefabInstanceID);
	json["NestedSlotID"] = UUIDToStringOrEmpty(canonical.nestedSlotID);
	json["IsPrefabAssetNested"] = canonical.isPrefabAssetNested;

	nlohmann::json entityMap = nlohmann::json::array();
	for (const auto& [prefabLocal, sceneLocal] : canonical.entityMap) {

		nlohmann::json pair = nlohmann::json::object();
		pair["P"] = UUIDToStringOrEmpty(prefabLocal);
		pair["S"] = UUIDToStringOrEmpty(sceneLocal);
		entityMap.push_back(std::move(pair));
	}
	json["EntityMap"] = std::move(entityMap);

	nlohmann::json modifications = nlohmann::json::array();
	for (const auto& mod : canonical.modifications) {

		nlohmann::json item = nlohmann::json::object();
		item["Target"] = UUIDToStringOrEmpty(mod.target);
		item["Path"] = mod.path;
		item["Value"] = mod.value;
		modifications.push_back(std::move(item));
	}
	json["Modifications"] = std::move(modifications);

	nlohmann::json addedComponents = nlohmann::json::array();
	for (const auto& added : canonical.addedComponents) {

		nlohmann::json item = nlohmann::json::object();
		item["Target"] = UUIDToStringOrEmpty(added.target);
		item["Type"] = added.type;
		item["Value"] = added.value;
		addedComponents.push_back(std::move(item));
	}
	json["AddedComponents"] = std::move(addedComponents);

	nlohmann::json removedComponents = nlohmann::json::array();
	for (const auto& removed : canonical.removedComponents) {

		nlohmann::json item = nlohmann::json::object();
		item["Target"] = UUIDToStringOrEmpty(removed.target);
		item["Type"] = removed.type;
		removedComponents.push_back(std::move(item));
	}
	json["RemovedComponents"] = std::move(removedComponents);

	nlohmann::json hierarchyMods = nlohmann::json::array();
	for (const auto& hierarchyMod : canonical.hierarchyModifications) {

		nlohmann::json item = nlohmann::json::object();
		item["Target"] = UUIDToStringOrEmpty(hierarchyMod.target);
		item["HasParent"] = hierarchyMod.hasParentOverride;
		item["NewParentPrefab"] = UUIDToStringOrEmpty(hierarchyMod.newParentPrefabLocalFileID);
		item["ExternalParent"] = UUIDToStringOrEmpty(hierarchyMod.externalParentSceneLocalFileID);
		item["HasSibling"] = hierarchyMod.hasSiblingOrder;
		item["Sibling"] = hierarchyMod.siblingOrder;
		hierarchyMods.push_back(std::move(item));
	}
	json["HierarchyMods"] = std::move(hierarchyMods);

	nlohmann::json removedEntities = nlohmann::json::array();
	for (const UUID& removed : canonical.removedEntities) {
		removedEntities.push_back(UUIDToStringOrEmpty(removed));
	}
	json["RemovedEntities"] = std::move(removedEntities);

	nlohmann::json addedEntities = nlohmann::json::array();
	for (const auto& added : canonical.addedEntities) {

		nlohmann::json item = nlohmann::json::object();
		item["SceneLocalFileID"] = UUIDToStringOrEmpty(added.sceneLocalFileID);
		item["Parent"] = UUIDToStringOrEmpty(added.parentSceneLocalFileID);
		item["Components"] = added.components;
		addedEntities.push_back(std::move(item));
	}
	json["AddedEntities"] = std::move(addedEntities);

	// 配列の並び順とは別に宣言元との対応を保存する
	std::sort(canonical.addedEntityMap.begin(), canonical.addedEntityMap.end(),
		[](const auto& lhs, const auto& rhs) { return lhs.first.value < rhs.first.value; });
	json["AddedEntityMap"] = nlohmann::json::array();
	for (const auto& [source, target] : canonical.addedEntityMap) {
		json["AddedEntityMap"].push_back({ { "P", UUIDToStringOrEmpty(source) }, { "S", UUIDToStringOrEmpty(target) } });
	}

	nlohmann::json nestedInstances = nlohmann::json::array();
	for (const auto& nested : canonical.nestedInstances) {
		nestedInstances.push_back(ToJson(nested));
	}
	json["NestedInstances"] = std::move(nestedInstances);

	nlohmann::json removedNestedSlots = nlohmann::json::array();
	for (UUID nestedSlotID : canonical.removedNestedSlots) {
		removedNestedSlots.push_back(UUIDToStringOrEmpty(nestedSlotID));
	}
	json["RemovedNestedSlots"] = std::move(removedNestedSlots);

	return json;
}

nlohmann::json Engine::ToSceneJson(const PrefabInstanceData& data) {

	PrefabInstanceData saved = data;
	const auto restoreIDs = [&](auto&& self, PrefabInstanceData& current, UUID owner) -> void {
		if (current.savedInstanceID) {
			current.instanceID = current.savedInstanceID;
		}
		current.ownerPrefabInstanceID = owner;
		for (auto& nested : current.nestedInstances) {
			self(self, nested, current.instanceID);
		}
	};
	restoreIDs(restoreIDs, saved, UUID{});
	return ToJson(saved);
}

bool Engine::FromJson(const nlohmann::json& json, PrefabInstanceData& output) try {

	static thread_local uint32_t readDepth = 0;
	if (readDepth >= 32) {
		return false;
	}
	struct ReadDepthGuard {

		uint32_t& depth;
		~ReadDepthGuard() { --depth; }
	};
	++readDepth;
	const ReadDepthGuard readDepthGuard{ readDepth };

	if (!json.is_object()) {
		return false;
	}

	// 存在する項目の型が違う場合は空配列へ読み替えない
	for (const char* key : { "EntityMap", "Modifications", "AddedComponents", "RemovedComponents", "HierarchyMods",
		"RemovedEntities", "AddedEntities", "AddedEntityMap", "NestedInstances", "RemovedNestedSlots" }) {
		if (json.contains(key) && !json[key].is_array()) {
			return false;
		}
	}
	PrefabInstanceData data;
	data.prefabAsset = StringToAssetGUIDOrZero(json.value("PrefabAsset", ""));
	data.instanceID = StringToUUIDOrZero(json.value("InstanceID", ""));
	data.rootParentSceneLocalFileID = StringToUUIDOrZero(json.value("RootParent", ""));
	data.ownerPrefabInstanceID = StringToUUIDOrZero(json.value("OwnerPrefabInstanceID", ""));
	data.nestedSlotID = StringToUUIDOrZero(json.value("NestedSlotID", ""));
	data.isPrefabAssetNested = json.value("IsPrefabAssetNested", false);

	if (json.contains("EntityMap") && json["EntityMap"].is_array()) {
		for (const auto& pair : json["EntityMap"]) {
			data.entityMap.emplace_back(
				StringToUUIDOrZero(pair.value("P", "")), StringToUUIDOrZero(pair.value("S", "")));
		}
	}
	if (json.contains("Modifications") && json["Modifications"].is_array()) {
		for (const auto& item : json["Modifications"]) {

			PrefabPropertyModification mod{};
			mod.target = StringToUUIDOrZero(item.value("Target", ""));
			mod.path = item.value("Path", "");
			mod.value = item.contains("Value") ? item["Value"] : nlohmann::json{};
			data.modifications.push_back(std::move(mod));
		}
	}
	if (json.contains("AddedComponents") && json["AddedComponents"].is_array()) {
		for (const auto& item : json["AddedComponents"]) {

			PrefabComponentModification added{};
			added.target = StringToUUIDOrZero(item.value("Target", ""));
			added.type = item.value("Type", "");
			added.value = item.contains("Value") ? item["Value"] : nlohmann::json{};
			data.addedComponents.push_back(std::move(added));
		}
	}
	if (json.contains("RemovedComponents") && json["RemovedComponents"].is_array()) {
		for (const auto& item : json["RemovedComponents"]) {

			PrefabComponentModification removed{};
			removed.target = StringToUUIDOrZero(item.value("Target", ""));
			removed.type = item.value("Type", "");
			data.removedComponents.push_back(std::move(removed));
		}
	}
	if (json.contains("HierarchyMods") && json["HierarchyMods"].is_array()) {
		for (const auto& item : json["HierarchyMods"]) {

			PrefabHierarchyModification hierarchyMod{};
			hierarchyMod.target = StringToUUIDOrZero(item.value("Target", ""));
			hierarchyMod.hasParentOverride = item.value("HasParent", false);
			hierarchyMod.newParentPrefabLocalFileID = StringToUUIDOrZero(item.value("NewParentPrefab", ""));
			hierarchyMod.externalParentSceneLocalFileID = StringToUUIDOrZero(item.value("ExternalParent", ""));
			hierarchyMod.hasSiblingOrder = item.value("HasSibling", false);
			hierarchyMod.siblingOrder = item.value("Sibling", 0);
			data.hierarchyModifications.push_back(std::move(hierarchyMod));
		}
	}
	if (json.contains("RemovedEntities") && json["RemovedEntities"].is_array()) {
		for (const auto& item : json["RemovedEntities"]) {
			if (!item.is_string()) {
				return false;
			}
			data.removedEntities.push_back(StringToUUIDOrZero(item.get<std::string>()));
		}
	}
	if (json.contains("AddedEntities") && json["AddedEntities"].is_array()) {
		for (const auto& item : json["AddedEntities"]) {

			PrefabAddedEntity added{};
			added.sceneLocalFileID = StringToUUIDOrZero(item.value("SceneLocalFileID", ""));
			added.parentSceneLocalFileID = StringToUUIDOrZero(item.value("Parent", ""));
			added.components = item.contains("Components") ? item["Components"] : nlohmann::json::object();
			data.addedEntities.push_back(std::move(added));
		}
	}
	if (json.contains("AddedEntityMap")) {
		for (const auto& entry : json["AddedEntityMap"]) {
			data.addedEntityMap.emplace_back(StringToUUIDOrZero(entry.at("P").get<std::string>()),
				StringToUUIDOrZero(entry.at("S").get<std::string>()));
		}
	}
	if (json.contains("NestedInstances") && json["NestedInstances"].is_array()) {
		for (const auto& item : json["NestedInstances"]) {

			PrefabInstanceData nested{};
			if (!FromJson(item, nested)) {
				return false;
			}
			data.nestedInstances.emplace_back(std::move(nested));
		}
	}
	if (json.contains("RemovedNestedSlots") && json["RemovedNestedSlots"].is_array()) {
		for (const auto& item : json["RemovedNestedSlots"]) {

			if (!item.is_string()) {
				return false;
			}
			data.removedNestedSlots.emplace_back(StringToUUIDOrZero(item.get<std::string>()));
		}
	}
	if (!data.prefabAsset || !data.instanceID || data.entityMap.empty()) {
		return false;
	}

	std::unordered_set<UUID> prefabLocalFileIDs;
	std::unordered_set<UUID> sceneLocalFileIDs;
	for (const auto& [prefabLocalFileID, sceneLocalFileID] : data.entityMap) {
		if (!prefabLocalFileID || !sceneLocalFileID ||
			!prefabLocalFileIDs.insert(prefabLocalFileID).second ||
			!sceneLocalFileIDs.insert(sceneLocalFileID).second) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[Prefab] Entity対応が不正です AssetID={} InstanceID={} PrefabID={} SceneID={}",
				ToString(data.prefabAsset), ToString(data.instanceID), ToString(prefabLocalFileID), ToString(sceneLocalFileID));
			return false;
		}
	}
	for (const auto& added : data.addedEntities) {
		if (!added.sceneLocalFileID || !added.components.is_object() ||
			!sceneLocalFileIDs.insert(added.sceneLocalFileID).second) {
			return false;
		}
	}
	std::unordered_set<UUID> addedIDs;
	for (const auto& added : data.addedEntities) {
		addedIDs.insert(added.sceneLocalFileID);
	}
	std::unordered_set<UUID> addedSources, addedTargets;
	for (const auto& [source, target] : data.addedEntityMap) {
		if (!source || !addedIDs.contains(target) || !addedSources.insert(source).second || !addedTargets.insert(target).second) {
			return false;
		}
	}
	std::unordered_set<UUID> nestedSlotIDs;
	for (const auto& nested : data.nestedInstances) {
		if (!nested.nestedSlotID ||
			(nested.ownerPrefabInstanceID && nested.ownerPrefabInstanceID != data.instanceID) ||
			!nestedSlotIDs.insert(nested.nestedSlotID).second) {
			return false;
		}
	}
	std::unordered_set<UUID> removedNestedSlotIDs;
	for (UUID nestedSlotID : data.removedNestedSlots) {
		if (!nestedSlotID || nestedSlotIDs.contains(nestedSlotID) ||
			!removedNestedSlotIDs.insert(nestedSlotID).second) {
			return false;
		}
	}
	// 全項目の検証後に以前の読込結果と差し替える
	output = std::move(data);
	return true;
} catch (const nlohmann::json::exception& error) {
	Logger::Output(LogType::Engine, spdlog::level::err, "[Prefab] 差分データの読込に失敗しました: {}", error.what());
	return false;
}
