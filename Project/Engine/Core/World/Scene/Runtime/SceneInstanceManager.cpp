#include "SceneInstanceManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <algorithm>
#include <cstdint>
#include <utility>
#include <unordered_map>
#include <unordered_set>

//============================================================================
//	SceneInstanceManager classMethods
//============================================================================
namespace {

	std::uint64_t MakeEntityKey(const Engine::Entity& entity) {

		return (static_cast<std::uint64_t>(entity.generation) << 32) | entity.index;
	}

	void AppendUniqueAlive(Engine::ECSWorld& world,
		std::vector<Engine::Entity>& entities, std::unordered_set<std::uint64_t>& entityKeys,
		const Engine::Entity& entity) {

		if (!world.IsAlive(entity)) {
			return;
		}
		if (!entityKeys.emplace(MakeEntityKey(entity)).second) {
			return;
		}
		entities.emplace_back(entity);
	}

	bool IsOwnedBySceneInstance(Engine::ECSWorld& world,
		const Engine::Entity& entity, Engine::UUID sceneInstanceID) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::SceneObjectComponent>(entity)) {
			return false;
		}
		const auto& sceneObject = world.GetComponent<Engine::SceneObjectComponent>(entity);
		return sceneObject.sceneInstanceID == sceneInstanceID;
	}

	bool HasNoSceneOwner(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::SceneObjectComponent>(entity)) {
			return true;
		}
		return !world.GetComponent<Engine::SceneObjectComponent>(entity).sceneInstanceID;
	}

}

bool Engine::SceneInstanceManager::LoadAdditive(AssetDatabase& database,
	const SceneSystem& sceneSystem, ECSWorld& world, AssetID sceneAsset, UUID forcedInstanceID) {

	if (forcedInstanceID && Find(forcedInstanceID)) {
		return false;
	}

	UUID instanceID{};
	if (!LoadSceneBranch(database, sceneSystem, world, sceneAsset, UUID{},
		forcedInstanceID, {}, instanceID)) {
		return false;
	}
	if (!active_) {
		active_ = instanceID;
	}
	++revision_;
	return true;
}

bool Engine::SceneInstanceManager::TryBeginSingleLoadRequest() {

	if (singleLoadRequestPending_) {
		return false;
	}
	singleLoadRequestPending_ = true;
	return true;
}

void Engine::SceneInstanceManager::ClearSingleLoadRequest() {

	singleLoadRequestPending_ = false;
}

Engine::UUID Engine::SceneInstanceManager::CreateScratchScene(const SceneHeader& header) {

	SceneInstance instance{};
	// ファイル実体を持たない一時シーンなのでsceneAssetは空、IDだけ新規採番する
	instance.instanceID = UUID::New();
	instance.parentInstanceID = UUID{};
	instance.sceneAsset = AssetID{};
	// スカイボックスやライティングを流用して、プレファブを通常シーンと同じ環境で見られるようにする
	instance.header = header;

	const UUID id = instance.instanceID;
	// 一時シーンを唯一のアクティブシーンにする
	active_ = id;
	scenes_.emplace_back(std::move(instance));
	++revision_;
	return id;
}

bool Engine::SceneInstanceManager::Unload(ECSWorld& world, UUID instanceID) {

	const SceneInstance* instance = Find(instanceID);
	if (!instance) {
		return false;
	}
	const UUID fallback = instance->parentInstanceID;
	if (!UnloadInternal(world, instanceID)) {
		return false;
	}
	if (active_ == instanceID || !Find(active_)) {
		active_ = Find(fallback) ? fallback : (scenes_.empty() ? UUID{} : scenes_.front().instanceID);
	}
	++revision_;
	return true;
}

void Engine::SceneInstanceManager::UnloadAll(ECSWorld& world) {

	while (!scenes_.empty()) {

		Unload(world, scenes_.back().instanceID);
	}
	active_ = UUID{};
	singleLoadRequestPending_ = false;
}

bool Engine::SceneInstanceManager::SaveActive(AssetDatabase& database, const SceneSystem& sceneSystem, ECSWorld& world) const {

	const SceneInstance* activeScene = GetActive();
	if (!activeScene || !activeScene->sceneAsset) {
		return false;
	}
	return Save(database, sceneSystem, world, activeScene->sceneAsset);
}

bool Engine::SceneInstanceManager::Save(AssetDatabase& database, const SceneSystem& sceneSystem,
	ECSWorld& world, AssetID sceneAsset) const {

	SceneSaveSnapshot snapshot{};
	if (!CaptureSave(database, sceneSystem, world,
		sceneAsset, snapshot)) {
		return false;
	}
	return SceneSystem::WriteSaveSnapshot(std::move(snapshot));
}

bool Engine::SceneInstanceManager::CaptureSave(
	AssetDatabase& database, const SceneSystem& sceneSystem,
	ECSWorld& world, AssetID sceneAsset,
	SceneSaveSnapshot& outSnapshot) const {

	const auto it = std::find_if(scenes_.begin(), scenes_.end(),
		[sceneAsset](const SceneInstance& scene) {
			return scene.sceneAsset == sceneAsset;
		});
	if (it == scenes_.end() || !sceneAsset) {
		return false;
	}

	const std::filesystem::path fullPath = database.ResolveFullPath(sceneAsset);
	if (fullPath.empty()) {
		return false;
	}

	const std::vector<Entity> ownedEntities = CollectSceneEntities(world, *it);
	return sceneSystem.CaptureSaveSnapshot(
		fullPath, world, it->header, database,
		outSnapshot, &ownedEntities);
}

nlohmann::json Engine::SceneInstanceManager::SerializeSnapshot(const SceneSystem& sceneSystem, ECSWorld& world) const {

	nlohmann::json root = nlohmann::json::object();

	// アクティブなインスタンスIDと全てのシーンインスタンスの情報をJSONに変換する
	root["ActiveInstance"] = ToString(active_);
	root["Scenes"] = nlohmann::json::array();

	for (const auto& scene : scenes_) {

		nlohmann::json sceneJson = nlohmann::json::object();

		sceneJson["InstanceID"] = ToString(scene.instanceID);
		sceneJson["ParentInstanceID"] = ToString(scene.parentInstanceID);
		sceneJson["SceneAsset"] = ToAssetReferenceJson(scene.sceneAsset);
		sceneJson["Header"] = ToJson(scene.header);

		const std::vector<Entity> ownedEntities = CollectSceneEntities(world, scene);
		sceneJson["Entities"] = sceneSystem.SerializeEntities(world, &ownedEntities);

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

bool Engine::SceneInstanceManager::LoadSnapshot(AssetDatabase& database, const SceneSystem& sceneSystem, ECSWorld& world, const nlohmann::json& snapshot) {

	scenes_.clear();
	active_ = UUID{};
	singleLoadRequestPending_ = false;
	if (!snapshot.is_object() || !snapshot.contains("Scenes") || !snapshot["Scenes"].is_array()) {
		++revision_;
		return false;
	}

	// JSONからアクティブなインスタンスIDと全てのシーンインスタンスの情報を読み込む
	std::string activeStr = snapshot.value("ActiveInstance", "");
	active_ = activeStr.empty() ? UUID{} : FromString16Hex(activeStr);
	for (auto& scene : snapshot["Scenes"]) {

		SceneInstance instance;

		instance.instanceID = FromString16Hex(scene.value("InstanceID", ""));
		instance.parentInstanceID = FromString16Hex(scene.value("ParentInstanceID", ""));
		instance.sceneAsset = FromString32Hex(scene.value("SceneAsset", ""));

		// 存在する場合のみヘッダーを読み込む
		if (scene.contains("Header")) {

			FromJson(scene["Header"], instance.header, &database);
		}
		{
			// PostProcessStackが未設定ならシーンごとの既定アセットを割り当てる
			const std::filesystem::path scenePath = database.ResolveFullPath(instance.sceneAsset);
			EnsureScenePostProcessStack(instance.header,
				Algorithm::PathToUTF8(scenePath), &database);
		}

		// エンティティ情報を読み込む
		nlohmann::json entityData = nlohmann::json::object();
		entityData["Entities"] = scene["Entities"];
		sceneSystem.LoadFromJson(entityData, world, &database, instance.sceneAsset, instance.instanceID, &instance.createdEntities);

		// 子シーンのリンク情報を読み込む
		if (scene.contains("Children") && scene["Children"].is_array()) {
			for (const auto& child : scene["Children"]) {

				SceneChildLink link{};
				link.slotID = FromString16Hex(child.value("SlotID", ""));
				link.slotName = child.value("SlotName", "");
				link.childInstanceID = FromString16Hex(child.value("ChildInstanceID", ""));
				instance.childScenes.emplace_back(std::move(link));
			}
		}

		// シーンインスタンスの情報をリストに追加する
		scenes_.push_back(std::move(instance));
	}
	// アクティブなインスタンスIDが存在しない場合は最初のインスタンスをアクティブにする
	if (!Find(active_) && !scenes_.empty()) {
		active_ = scenes_.front().instanceID;
	}
	++revision_;
	return true;
}

bool Engine::SceneInstanceManager::LoadSceneTree(AssetDatabase& database,
	const SceneSystem& sceneSystem, ECSWorld& world, AssetID rootAsset) {

	// 現在のツリーを実体ごと破棄してから新しいルートをロードする
	UnloadAll(world);
	active_ = UUID{};
	singleLoadRequestPending_ = false;
	UUID rootInstanceID{};
	if (!LoadSceneBranch(database, sceneSystem, world, rootAsset,
		UUID{}, UUID{}, {}, rootInstanceID)) {
		UnloadAll(world);
		active_ = UUID{};
		++revision_;
		return false;
	}
	active_ = rootInstanceID;
	++revision_;
	return true;
}

bool Engine::SceneInstanceManager::SynchronizeSubScenes(AssetDatabase& database,
	const SceneSystem& sceneSystem, ECSWorld& world, UUID parentInstanceID) {

	SceneInstance* parent = Find(parentInstanceID);
	if (!parent) {
		return false;
	}
	const std::vector<SubSceneSlotDesc> desiredSlots = parent->header.subScenes;
	std::unordered_set<UUID> slotIDs;
	std::unordered_set<std::string> slotNames;
	for (const SubSceneSlotDesc& slot : desiredSlots) {
		if (!slot.slotID || !slotIDs.insert(slot.slotID).second ||
			slot.slotName.empty() || !slotNames.insert(slot.slotName).second) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[SubScene] slot ID or name is empty or duplicated. parent={} slot={}",
				ToString(parentInstanceID), slot.slotName);
			return false;
		}
	}

	std::unordered_map<UUID, SceneChildLink> existing;
	for (const SceneChildLink& link : parent->childScenes) {
		existing.emplace(link.slotID, link);
	}

	std::vector<AssetID> ancestors;
	for (const SceneInstance* current = parent; current; current = Find(current->parentInstanceID)) {
		ancestors.emplace_back(current->sceneAsset);
	}
	std::reverse(ancestors.begin(), ancestors.end());

	std::vector<SceneChildLink> nextLinks;
	for (const SubSceneSlotDesc& slot : desiredSlots) {

		auto found = existing.find(slot.slotID);
		const SceneInstance* existingChild =
			found == existing.end() ? nullptr : Find(found->second.childInstanceID);
		const UUID existingChildID = existingChild ? existingChild->instanceID : UUID{};
		const AssetID existingChildAsset = existingChild ? existingChild->sceneAsset : AssetID{};
		if (!slot.enabled || !slot.sceneAsset) {
			if (existingChildID) {
				UnloadInternal(world, existingChildID);
			}
			if (found != existing.end()) {
				existing.erase(found);
			}
			continue;
		}
		if (existingChildID && existingChildAsset == slot.sceneAsset) {
			SceneChildLink link = found->second;
			link.slotName = slot.slotName;
			nextLinks.emplace_back(std::move(link));
			existing.erase(found);
			continue;
		}

		UUID childInstanceID{};
		if (!LoadSceneBranch(database, sceneSystem, world, slot.sceneAsset,
			parentInstanceID, UUID{}, ancestors, childInstanceID)) {
			return false;
		}
		if (existingChildID) {
			UnloadInternal(world, existingChildID);
		}
		if (found != existing.end()) {
			existing.erase(found);
		}
		nextLinks.push_back({ slot.slotID, slot.slotName, childInstanceID });
	}

	for (const auto& [slotID, link] : existing) {
		UnloadInternal(world, link.childInstanceID);
	}
	parent = Find(parentInstanceID);
	if (!parent) {
		return false;
	}
	parent->childScenes = std::move(nextLinks);
	++revision_;
	return true;
}

bool Engine::SceneInstanceManager::LoadSceneBranch(AssetDatabase& database,
	const SceneSystem& sceneSystem, ECSWorld& world, AssetID sceneAsset,
	UUID parentInstanceID, UUID forcedInstanceID,
	const std::vector<AssetID>& ancestors, UUID& outInstanceID) {

	outInstanceID = UUID{};
	if (!sceneAsset || std::find(ancestors.begin(), ancestors.end(), sceneAsset) != ancestors.end()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SubScene] cyclic scene reference. asset={}", ToString(sceneAsset));
		return false;
	}
	const std::filesystem::path path = database.ResolveFullPath(sceneAsset);
	if (path.empty()) {
		return false;
	}

	SceneInstance instance{};
	instance.instanceID = forcedInstanceID ? forcedInstanceID : UUID::New();
	if (Find(instance.instanceID)) {
		return false;
	}
	instance.parentInstanceID = parentInstanceID;
	instance.sceneAsset = sceneAsset;
	if (!sceneSystem.LoadScene(path, world, &database, sceneAsset, instance.instanceID,
		&instance.header, &instance.createdEntities)) {
		return false;
	}

	const UUID instanceID = instance.instanceID;
	const std::vector<SubSceneSlotDesc> slots = instance.header.subScenes;
	scenes_.emplace_back(std::move(instance));

	std::vector<AssetID> childAncestors = ancestors;
	childAncestors.emplace_back(sceneAsset);
	std::unordered_set<UUID> slotIDs;
	std::unordered_set<std::string> slotNames;
	for (const SubSceneSlotDesc& slot : slots) {

		if (!slot.slotID || !slotIDs.insert(slot.slotID).second ||
			slot.slotName.empty() || !slotNames.insert(slot.slotName).second) {
			UnloadInternal(world, instanceID);
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[SubScene] slot ID or name is empty or duplicated. asset={} slot={}",
				ToString(sceneAsset), slot.slotName);
			return false;
		}
		if (!slot.enabled || !slot.sceneAsset) {
			continue;
		}

		UUID childInstanceID{};
		if (!LoadSceneBranch(database, sceneSystem, world, slot.sceneAsset,
			instanceID, UUID{}, childAncestors, childInstanceID)) {
			UnloadInternal(world, instanceID);
			return false;
		}
		SceneInstance* current = Find(instanceID);
		if (!current) {
			UnloadInternal(world, childInstanceID);
			return false;
		}
		current->childScenes.push_back({ slot.slotID, slot.slotName, childInstanceID });
	}
	outInstanceID = instanceID;
	return true;
}

bool Engine::SceneInstanceManager::UnloadInternal(ECSWorld& world, UUID instanceID) {

	SceneInstance* instance = Find(instanceID);
	if (!instance) {
		return false;
	}
	const UUID parentInstanceID = instance->parentInstanceID;
	const std::vector<SceneChildLink> children = instance->childScenes;
	for (const SceneChildLink& child : children) {
		UnloadInternal(world, child.childInstanceID);
	}

	instance = Find(instanceID);
	if (!instance) {
		return false;
	}
	const std::vector<Entity> ownedEntities = CollectSceneEntities(world, *instance);
	for (const Entity& entity : ownedEntities) {
		if (world.IsAlive(entity)) {
			world.DestroyEntity(entity);
		}
	}
	world.FlushPendingDestroyEntities();

	scenes_.erase(std::remove_if(scenes_.begin(), scenes_.end(),
		[instanceID](const SceneInstance& scene) { return scene.instanceID == instanceID; }),
		scenes_.end());
	for (SceneInstance& scene : scenes_) {
		std::erase_if(scene.childScenes, [instanceID](const SceneChildLink& link) {
			return link.childInstanceID == instanceID;
			});
	}
	if (active_ == instanceID || !Find(active_)) {
		active_ = Find(parentInstanceID) ? parentInstanceID :
			(scenes_.empty() ? UUID{} : scenes_.front().instanceID);
	}
	return true;
}

void Engine::SceneInstanceManager::SetActive(UUID instanceID) {

	// インスタンスIDからシーンインスタンスを探して、存在すればアクティブにする
	if (active_ != instanceID && Find(instanceID)) {

		active_ = instanceID;
		++revision_;
	}
}

const Engine::SceneInstance* Engine::SceneInstanceManager::GetActive() const {

	// アクティブなシーンインスタンスをリストから探して返す
	for (const auto& scene : scenes_) {
		if (scene.instanceID == active_) {
			return &scene;
		}
	}
	return nullptr;
}

std::vector<Engine::Entity> Engine::SceneInstanceManager::CollectSceneEntities(ECSWorld& world, const SceneInstance& scene) {

	std::vector<Entity> entities;
	entities.reserve(scene.createdEntities.size());
	std::unordered_set<std::uint64_t> entityKeys;
	entityKeys.reserve(scene.createdEntities.size());

	world.ForEachAliveEntity([&](Entity entity) {
		if (IsOwnedBySceneInstance(world, entity, scene.instanceID)) {

			AppendUniqueAlive(world, entities, entityKeys, entity);
		}
		});

	for (const auto& entity : scene.createdEntities) {
		if (IsOwnedBySceneInstance(world, entity, scene.instanceID) || HasNoSceneOwner(world, entity)) {

			AppendUniqueAlive(world, entities, entityKeys, entity);
		}
	}
	return entities;
}

const Engine::SceneInstance* Engine::SceneInstanceManager::Find(UUID id) const {

	// UUIDからシーンインスタンスを検索する関数
	for (auto& scene : scenes_) {
		if (scene.instanceID == id) {
			return &scene;
		}
	}
	return nullptr;
}

Engine::SceneInstance* Engine::SceneInstanceManager::Find(UUID id) {

	// UUIDからシーンインスタンスを検索する関数
	for (auto& scene : scenes_) {
		if (scene.instanceID == id) {
			return &scene;
		}
	}
	return nullptr;
}

const Engine::SceneInstance* Engine::SceneInstanceManager::FindChildBySlot(
	UUID parentID, const std::string_view& slotName) const {

	const SceneInstance* parent = Find(parentID);
	if (!parent) {
		return nullptr;
	}
	// 子シーンの中でスロット名が一致したシーン返す
	for (const auto& childLink : parent->childScenes) {
		if (childLink.slotName == slotName) {
			return Find(childLink.childInstanceID);
		}
	}
	return nullptr;
}
