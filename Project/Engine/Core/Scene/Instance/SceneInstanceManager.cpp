#include "SceneInstanceManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>

// c++
#include <algorithm>

//============================================================================
//	SceneInstanceManager classMethods
//============================================================================

namespace {

	bool ContainsEntity(const std::vector<Engine::Entity>& entities, const Engine::Entity& entity) {

		return std::find(entities.begin(), entities.end(), entity) != entities.end();
	}

	void AppendUniqueAlive(Engine::ECSWorld& world,
		std::vector<Engine::Entity>& entities, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || ContainsEntity(entities, entity)) {
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
	const SceneSystem& sceneSystem, ECSWorld& world, AssetID sceneAsset) {

	// アセットIDからファイルパスを取得する
	auto path = database.ResolveFullPath(sceneAsset);
	if (path.empty()) {
		return false;
	}

	SceneInstance instance{};
	instance.instanceID = UUID::New();
	instance.parentInstanceID = UUID{};
	instance.sceneAsset = sceneAsset;

	// ファイルからヘッダ、エンティティを読み込む
	if (!sceneSystem.LoadScene(path.string(), world, &database, sceneAsset, instance.instanceID,
		&instance.header, &instance.createdEntities)) {
		return false;
	}

	// アクティブ未設定なら初回をアクティブにする
	if (!active_) {

		active_ = instance.instanceID;
	}

	// インスタンスをリストに追加する
	scenes_.emplace_back(std::move(instance));
	return true;
}

bool Engine::SceneInstanceManager::Unload(ECSWorld& world, UUID instanceID) {

	for (size_t i = 0; i < scenes_.size(); ++i) {

		// インスタンスIDが一致するものを探す
		if (scenes_[i].instanceID != instanceID) {
			continue;
		}

		// 作ったエンティティを破棄
		const std::vector<Entity> ownedEntities = CollectSceneEntities(world, scenes_[i]);
		for (const auto& entity : ownedEntities) {
			if (world.IsAlive(entity)) {

				world.DestroyEntity(entity);
			}
		}

		// インスタンスをリストから消す
		scenes_.erase(scenes_.begin() + i);

		// アクティブなインスタンスが削除されたら
		if (active_ == instanceID) {

			active_ = scenes_.empty() ? UUID{} : scenes_.front().instanceID;
		}
		return true;
	}
	return false;
}

void Engine::SceneInstanceManager::UnloadAll(ECSWorld& world) {

	while (!scenes_.empty()) {

		Unload(world, scenes_.back().instanceID);
	}
	active_ = UUID{};
}

bool Engine::SceneInstanceManager::SaveActive(AssetDatabase& database, const SceneSystem& sceneSystem, ECSWorld& world) const {

	const SceneInstance* activeScene = GetActive();
	if (!activeScene || !activeScene->sceneAsset) {
		return false;
	}

	const std::filesystem::path fullPath = database.ResolveFullPath(activeScene->sceneAsset);
	if (fullPath.empty()) {
		return false;
	}

	const std::vector<Entity> ownedEntities = CollectSceneEntities(world, *activeScene);
	return sceneSystem.SaveScene(fullPath.string(), world, activeScene->header, &ownedEntities);
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
		sceneJson["SceneAsset"] = ToString(scene.sceneAsset);
		sceneJson["Header"] = ToJson(scene.header);

		const std::vector<Entity> ownedEntities = CollectSceneEntities(world, scene);
		sceneJson["Entities"] = sceneSystem.SerializeEntities(world, &ownedEntities);

		sceneJson["Children"] = nlohmann::json::array();
		for (const auto& child : scene.childScenes) {
			nlohmann::json childJson = nlohmann::json::object();
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
	if (!snapshot.is_object() || !snapshot.contains("Scenes") || !snapshot["Scenes"].is_array()) {
		return false;
	}

	// JSONからアクティブなインスタンスIDと全てのシーンインスタンスの情報を読み込む
	std::string activeStr = snapshot.value("ActiveInstance", "");
	active_ = activeStr.empty() ? UUID{} : FromString16Hex(activeStr);
	for (auto& scene : snapshot["Scenes"]) {

		SceneInstance instance;

		instance.instanceID = FromString16Hex(scene.value("InstanceID", ""));
		instance.parentInstanceID = FromString16Hex(scene.value("ParentInstanceID", ""));
		instance.sceneAsset = FromString16Hex(scene.value("SceneAsset", ""));

		// 存在する場合のみヘッダーを読み込む
		if (scene.contains("Header")) {

			FromJson(scene["Header"], instance.header, &database);
		}

		// エンティティ情報を読み込む
		nlohmann::json entityData = nlohmann::json::object();
		entityData["Entities"] = scene["Entities"];
		sceneSystem.LoadFromJson(entityData, world, &database, instance.sceneAsset, instance.instanceID, &instance.createdEntities);

		// 子シーンのリンク情報を読み込む
		if (scene.contains("Children") && scene["Children"].is_array()) {
			for (const auto& child : scene["Children"]) {

				SceneChildLink link{};
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
	return true;
}

bool Engine::SceneInstanceManager::LoadSceneTree(AssetDatabase& database,
	const SceneSystem& sceneSystem, ECSWorld& world, AssetID rootAsset) {

	// シーンをすべてクリアしてから、再帰的にシーンツリーをロードする
	scenes_.clear();
	active_ = UUID{};
	std::function<UUID(AssetID, UUID)> loadRecursive = [&](AssetID sceneAsset, UUID parentInstanceID) -> UUID {

		auto path = database.ResolveFullPath(sceneAsset);
		if (path.empty()) {
			return UUID{};
		}

		// シーンインスタンス初期化
		SceneInstance instance{};
		instance.instanceID = UUID::New();
		instance.parentInstanceID = parentInstanceID;
		instance.sceneAsset = sceneAsset;
		// ファイルからヘッダ、エンティティを読み込む
		if (!sceneSystem.LoadScene(path.string(), world, &database, sceneAsset, instance.instanceID,
			&instance.header, &instance.createdEntities)) {
			return UUID{};
		}

		// インスタンスをリストに追加する
		const UUID instanceID = instance.instanceID;
		scenes_.emplace_back(std::move(instance));
		SceneInstance* current = Find(instanceID);
		if (!current) {
			return UUID{};
		}
		// サブシーンのスロットを順番に処理する
		for (const auto& subScene : current->header.subScenes) {
			if (!subScene.enabled || !subScene.sceneAsset) {
				continue;
			}

			// サブシーンを再帰的にロードしてインスタンスIDを取得する
			const UUID childInstanceID = loadRecursive(subScene.sceneAsset, instanceID);
			if (!childInstanceID) {
				return UUID{};
			}
			current = Find(instanceID);
			if (!current) {
				return UUID{};
			}
			// 子シーンのリンク情報を親シーンのインスタンスに追加する
			SceneChildLink link{};
			link.slotName = subScene.slotName;
			link.childInstanceID = childInstanceID;
			current->childScenes.emplace_back(std::move(link));
		}
		return instanceID;
		};

	// ルートシーンから再帰的にシーンツリーをロードする
	const UUID rootInstanceID = loadRecursive(rootAsset, UUID{});
	// ルートシーンのロードに失敗した場合は全てクリアして失敗を返す
	if (!rootInstanceID) {
		scenes_.clear();
		active_ = UUID{};
		return false;
	}
	active_ = rootInstanceID;
	return true;
}

void Engine::SceneInstanceManager::SetActive(UUID instanceID) {

	// インスタンスIDからシーンインスタンスを探して、存在すればアクティブにする
	if (Find(instanceID)) {

		active_ = instanceID;
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

	world.ForEachAliveEntity([&](Entity entity) {
		if (IsOwnedBySceneInstance(world, entity, scene.instanceID)) {

			AppendUniqueAlive(world, entities, entity);
		}
		});

	for (const auto& entity : scene.createdEntities) {
		if (IsOwnedBySceneInstance(world, entity, scene.instanceID) || HasNoSceneOwner(world, entity)) {

			AppendUniqueAlive(world, entities, entity);
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
	UUID parentId, const std::string_view& slotName) const {

	const SceneInstance* parent = Find(parentId);
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
