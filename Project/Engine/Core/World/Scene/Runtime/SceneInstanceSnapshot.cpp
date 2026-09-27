#include "SceneInstanceManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <stdexcept>
#include <unordered_set>

nlohmann::json Engine::SceneInstanceManager::SerializeSnapshot(const SceneSystem& sceneSystem, ECSWorld& world) const {

	nlohmann::json root = nlohmann::json::object();

	// アクティブなインスタンスIDと全てのシーンインスタンスの情報をJSONに変換する
	root["ActiveInstance"] = ToString(active_);
	root["Scenes"] = nlohmann::json::array();

	for (const auto& scene : scenes_) {

		if (scene.persistent) {
			continue;
		}
		nlohmann::json sceneJson = nlohmann::json::object();

		sceneJson["InstanceID"] = ToString(scene.instanceID);
		sceneJson["ParentInstanceID"] = ToString(scene.parentInstanceID);
		sceneJson["SceneAsset"] = ToAssetReferenceJson(scene.sceneAsset);
		sceneJson["Header"] = ToJson(scene.header);

		const std::vector<Entity> ownedEntities = CollectSceneEntities(world, scene);
		sceneJson["Entities"] = sceneSystem.SerializeEntities(world, &ownedEntities);
		// Prefab由来の参照空間をWorld切替後も維持する
		sceneJson["SourceAssets"] = nlohmann::json::object();
		for (Entity entity : ownedEntities) {
			const auto& object = world.GetComponent<SceneObjectComponent>(entity);
			sceneJson["SourceAssets"][ToString(object.localFileID)] = ToAssetReferenceJson(object.sourceAsset);
		}

		sceneJson["Children"] = nlohmann::json::array();
		for (const auto& child : scene.childScenes) {
			nlohmann::json childJson = nlohmann::json::object();
			childJson["SlotID"] = ToString(child.slotID);
			childJson["SlotName"] = child.slotName;
			childJson["ChildInstanceID"] = ToString(child.childInstanceID);
			sceneJson["Children"].push_back(childJson);
		}

		// シーンインスタンスの情報を配列に追加する
		root["Scenes"].push_back(sceneJson);
	}
	return root;
}

bool Engine::SceneInstanceManager::LoadSnapshot(AssetDatabase& database, const SceneSystem& sceneSystem,
	ECSWorld& world, const nlohmann::json& snapshot) {

	// Snapshotは新しいWorldへ読み込み、既存の実体は置き換えない
	bool occupied = !scenes_.empty();
	world.ForEachAliveEntity([&](Entity) { occupied = true; });
	if (occupied) {
		return false;
	}
	SceneInstanceManager candidate;
	try {
		if (!snapshot.is_object() || !snapshot.contains("Scenes") || !snapshot["Scenes"].is_array()) {
			return false;
		}
		candidate.active_ = FromString16Hex(snapshot.value("ActiveInstance", ""));
		std::unordered_set<UUID> identifiers;
		// 所属とリンクをすべて確認してから実体を生成する
		for (const auto& scene : snapshot["Scenes"]) {
			if (!scene.is_object() || !scene.contains("Entities") || !scene["Entities"].is_array()) {
				throw std::runtime_error("SnapshotのEntity配列が不正です");
			}
			SceneInstance instance;
			instance.instanceID = FromString16Hex(scene.value("InstanceID", ""));
			instance.parentInstanceID = FromString16Hex(scene.value("ParentInstanceID", ""));
			instance.sceneAsset = FromString32Hex(scene.value("SceneAsset", ""));
			if (!instance.instanceID || !identifiers.insert(instance.instanceID).second) {
				throw std::runtime_error("SnapshotのScene Instance IDが不正または重複しています");
			}
			if (scene.contains("Header") && !FromJson(scene["Header"], instance.header, &database)) {
				throw std::runtime_error("SnapshotのScene Headerが不正です");
			}
			if (scene.contains("Children")) {
				if (!scene["Children"].is_array()) {
					throw std::runtime_error("Snapshotの子Scene配列が不正です");
				}
				std::unordered_set<UUID> slots;
				for (const auto& child : scene["Children"]) {
					SceneChildLink link;
					link.slotID = FromString16Hex(child.value("SlotID", ""));
					link.slotName = child.value("SlotName", "");
					link.childInstanceID = FromString16Hex(child.value("ChildInstanceID", ""));
					if (!link.slotID || !link.childInstanceID || !slots.insert(link.slotID).second) {
						throw std::runtime_error("Snapshotの子Sceneリンクが不正です");
					}
					instance.childScenes.emplace_back(std::move(link));
				}
			}
			candidate.scenes_.emplace_back(std::move(instance));
		}
		std::unordered_set<UUID> linkedChildren;
		for (const auto& instance : candidate.scenes_) {
			for (const auto& link : instance.childScenes) {
				const auto* child = candidate.Find(link.childInstanceID);
				if (!child || child->parentInstanceID != instance.instanceID ||
					!linkedChildren.insert(link.childInstanceID).second) {
					throw std::runtime_error("Snapshotの親子Sceneが一致しません");
				}
			}
			std::unordered_set<UUID> ancestors{ instance.instanceID };
			for (UUID parent = instance.parentInstanceID; parent;) {
				const auto* ancestor = candidate.Find(parent);
				if (!ancestor || !ancestors.insert(parent).second) {
					throw std::runtime_error("Snapshotの親Sceneが欠損または循環しています");
				}
				parent = ancestor->parentInstanceID;
			}
		}
		for (const auto& instance : candidate.scenes_) {
			if (instance.parentInstanceID && !linkedChildren.contains(instance.instanceID)) {
				throw std::runtime_error("Snapshotの子Sceneリンクが欠損しています");
			}
		}
		for (size_t index = 0; index < candidate.scenes_.size(); ++index) {
			auto& instance = candidate.scenes_[index];
			const auto scenePath = database.ResolveFullPath(instance.sceneAsset);
			EnsureSceneRenderFeatureProfile(instance.header, Algorithm::PathToUTF8(scenePath), &database);
			const nlohmann::json entities{ { "Entities", snapshot["Scenes"][index]["Entities"] } };
			if (!sceneSystem.LoadFromJson(entities, world, &database, instance.sceneAsset,
				instance.instanceID, &instance.createdEntities)) {
				throw std::runtime_error("SnapshotのEntityを復元できません");
			}
			const auto sources = snapshot["Scenes"][index].find("SourceAssets");
			if (sources != snapshot["Scenes"][index].end()) {
				if (!sources->is_object()) {
					throw std::runtime_error("Snapshotの参照元Asset表が不正です");
				}
				for (Entity entity : instance.createdEntities) {
					auto& object = world.GetComponent<SceneObjectComponent>(entity);
					const auto entry = sources->find(ToString(object.localFileID));
					if (entry == sources->end() || !entry->is_string()) {
						throw std::runtime_error("Snapshotの参照元Assetがありません");
					}
					const auto source = entry->get<std::string>();
					const AssetID asset = FromString32Hex(source);
					if (!source.empty() && !asset) {
						throw std::runtime_error("Snapshotの参照元Asset IDが不正です");
					}
					object.sourceAsset = asset;
				}
			}
		}
	} catch (const std::exception& error) {

		// 途中生成した実体だけを破棄し、Scene構成は公開しない
		std::vector<Entity> created;
		world.ForEachAliveEntity([&](Entity entity) { created.emplace_back(entity); });
		for (auto it = created.rbegin(); it != created.rend(); ++it) {
			world.DestroyEntity(*it);
		}
		world.FlushPendingDestroyEntities();
		Logger::Output(LogType::Engine, spdlog::level::err, "[SceneSnapshot] 読込に失敗しました 詳細={}", error.what());
		return false;
	}
	if (!candidate.Find(candidate.active_) && !candidate.scenes_.empty()) {
		candidate.active_ = candidate.scenes_.front().instanceID;
	}
	scenes_ = std::move(candidate.scenes_);
	active_ = candidate.active_;
	singleLoadRequestPending_ = false;
	++revision_;
	return true;
}
