#include "SceneInstanceManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>

// c++
#include <algorithm>
#include <cstdint>
#include <iterator>
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

bool Engine::SceneInstanceManager::DontDestroyOnLoad(ECSWorld& world, Entity root) {

	if (!world.IsAlive(root) || world.IsPendingDestroy(root)) {
		return false;
	}
	const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(root);
	if (hierarchy && world.IsAlive(hierarchy->parent)) {
		return false;
	}
	const std::vector<Entity> entities = HierarchyUtility::CollectLogicalSubtree(world, root);
	for (Entity entity : entities) {
		if (!world.HasComponent<SceneObjectComponent>(entity) || world.IsPendingDestroy(entity)) {
			return false;
		}
	}
	const UUID owner = world.GetComponent<SceneObjectComponent>(root).sceneInstanceID;
	if (const SceneInstance* scene = Find(owner); scene && scene->persistent) {
		return true;
	}
	auto persistent = std::find_if(scenes_.begin(), scenes_.end(),
		[](const SceneInstance& scene) { return scene.persistent; });
	if (persistent == scenes_.end()) {
		SceneInstance scene{};
		scene.instanceID = UUID::New();
		scene.persistent = true;
		scene.header.name = "DontDestroyOnLoad";
		scenes_.emplace_back(std::move(scene));
		persistent = std::prev(scenes_.end());
	}
	const UUID destination = persistent->instanceID;
	persistent->createdEntities.reserve(persistent->createdEntities.size() + entities.size());
	std::unordered_set<std::uint64_t> moved;
	for (Entity entity : entities) {
		moved.emplace(MakeEntityKey(entity));
		world.GetComponent<SceneObjectComponent>(entity).sceneInstanceID = destination;
		world.MarkComponentModified<SceneObjectComponent>(entity);
		persistent->createdEntities.emplace_back(entity);
	}
	for (SceneInstance& scene : scenes_) {
		if (!scene.persistent) {
			std::erase_if(scene.createdEntities,
				[&](Entity entity) { return moved.contains(MakeEntityKey(entity)); });
		}
	}
	++revision_;
	return true;
}

Engine::UUID Engine::SceneInstanceManager::FindFirstRegularScene() const {

	for (const SceneInstance& scene : scenes_) {
		if (!scene.persistent) {
			return scene.instanceID;
		}
	}
	return UUID{};
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
	if (!instance || instance->persistent) {
		return false;
	}
	const UUID fallback = instance->parentInstanceID;
	if (!UnloadInternal(world, instanceID)) {
		return false;
	}
	if (active_ == instanceID || !Find(active_)) {
		active_ = Find(fallback) ? fallback : FindFirstRegularScene();
	}
	++revision_;
	return true;
}

void Engine::SceneInstanceManager::UnloadAll(ECSWorld& world) {

	while (!scenes_.empty()) {

		UnloadInternal(world, scenes_.back().instanceID);
		++revision_;
	}
	active_ = UUID{};
	singleLoadRequestPending_ = false;
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
			FindFirstRegularScene();
	}
	return true;
}

void Engine::SceneInstanceManager::SetActive(UUID instanceID) {

	// インスタンスIDからシーンインスタンスを探して、存在すればアクティブにする
	const SceneInstance* scene = Find(instanceID);
	if (active_ != instanceID && scene && !scene->persistent) {

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

std::vector<Engine::UUID> Engine::SceneInstanceManager::FindInstanceIDs(AssetID sceneAsset) const {

	std::vector<UUID> instanceIDs;
	for (const SceneInstance& scene : scenes_) {
		if (scene.sceneAsset == sceneAsset && !scene.persistent) {
			instanceIDs.emplace_back(scene.instanceID);
		}
	}
	return instanceIDs;
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
