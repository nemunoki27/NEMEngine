#include "WorldCommandBuffer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

namespace {

	using namespace Engine;

	// entityとその子孫をまとめて破棄予約する
	void DestroyEntitySubtree(ECSWorld& world, const Entity& entity) {

		const std::vector<Entity> entities = HierarchyUtility::CollectLogicalSubtree(world, entity);
		for (auto it = entities.rbegin(); it != entities.rend(); ++it) {

			if (world.IsAlive(*it)) {
				world.DestroyEntity(*it);
			}
		}
	}

	// childをnewParentの子にすると循環するか、newParentの祖先にchildが居るか
	bool WouldCreateCycle(ECSWorld& world, const Entity& child, const Entity& newParent) {

		Entity current = newParent;
		for (int32_t guard = 0; guard < 4096; ++guard) {
			if (!world.IsAlive(current)) {
				return false;
			}
			if (current == child) {
				return true;
			}
			HierarchyComponent* hierarchy = world.TryGetComponent<HierarchyComponent>(current);
			if (!hierarchy || !world.IsAlive(hierarchy->parent)) {
				return false;
			}
			current = hierarchy->parent;
		}
		return true;
	}

	// worldPositionStays:親変更前のchild world姿勢を、変更後の親追従Transform基準のlocal値へ落として維持する
	void PreserveWorldTransform(ECSWorld& world, const Entity& child,
		const ResolvedWorldTransform& childWorldBefore) {

		TransformComponent* childTransform = world.TryGetComponent<TransformComponent>(child);
		if (!childTransform) {
			return;
		}
		ResolvedWorldTransform parentFollow{};
		if (!TransformWorldUtility::ResolveParentFollowTransform(world, child, parentFollow)) {
			return;
		}

		const Vector3 worldPos = childWorldBefore.matrix.GetTranslationValue();
		childTransform->localPos = Vector3::Transform(
			worldPos, Matrix4x4::Inverse(parentFollow.matrix));
		childTransform->localRotation = Quaternion::Normalize(
			Quaternion::Inverse(parentFollow.rotation) * childWorldBefore.rotation);
		childTransform->localScale = Vector3(
			parentFollow.scale.x != 0.0f ? childWorldBefore.scale.x / parentFollow.scale.x : childWorldBefore.scale.x,
			parentFollow.scale.y != 0.0f ? childWorldBefore.scale.y / parentFollow.scale.y : childWorldBefore.scale.y,
			parentFollow.scale.z != 0.0f ? childWorldBefore.scale.z / parentFollow.scale.z : childWorldBefore.scale.z);
	}
}

//============================================================================
//	WorldCommandBuffer classMethods
//============================================================================
void Engine::WorldCommandBuffer::EnqueueDestroyEntity(const Entity& entity) {

	Command command{};
	command.kind = CommandKind::DestroyEntity;
	command.target = entity;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueAddComponentByName(const Entity& entity, std::string_view typeName) {

	Command command{};
	command.kind = CommandKind::AddComponentByName;
	command.target = entity;
	command.text.assign(typeName);
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueRemoveComponentByName(const Entity& entity, std::string_view typeName) {

	Command command{};
	command.kind = CommandKind::RemoveComponentByName;
	command.target = entity;
	command.text.assign(typeName);
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueSetNameEnsuringComponent(const Entity& entity, std::string_view name) {

	Command command{};
	command.kind = CommandKind::SetNameEnsuringComponent;
	command.target = entity;
	command.text.assign(name);
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueSetActiveSelfEnsuringComponent(const Entity& entity, bool active) {

	Command command{};
	command.kind = CommandKind::SetActiveSelfEnsuringComponent;
	command.target = entity;
	command.boolValue = active;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueSetParent(const Entity& child, const Entity& parent, bool worldPositionStays) {

	Command command{};
	command.kind = CommandKind::SetParent;
	command.target = child;
	command.parent = parent;
	command.boolValue = worldPositionStays;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueCreateEntity(const Entity& reserved, std::string_view name, const Entity& parent) {

	Command command{};
	command.kind = CommandKind::CreateEntity;
	command.target = reserved;
	command.parent = parent;
	command.text.assign(name);
	commands_.emplace_back(std::move(command));
	// 予約Entityからこのコマンドのindexを引けるよう記録する
	createCommandIndex_[EntityKey(reserved)] = commands_.size() - 1;
}

void Engine::WorldCommandBuffer::EnqueueInstantiatePrefab(const Entity& reservedRoot, const UUID& prefabAsset,
	const Vector3& position, const Quaternion& rotation, bool useTransform, const Entity& parent) {

	Command command{};
	command.kind = CommandKind::InstantiatePrefab;
	command.target = reservedRoot;
	command.parent = parent;
	command.assetID = prefabAsset;
	command.position = position;
	command.rotation = rotation;
	if (useTransform) {
		command.flags |= FlagUseTransform;
	}
	commands_.emplace_back(std::move(command));
	// 予約ルートEntityからこのコマンドのindexを引けるよう記録する
	createCommandIndex_[EntityKey(reservedRoot)] = commands_.size() - 1;
}

void Engine::WorldCommandBuffer::EnqueueLoadSceneAdditive(const UUID& sceneInstanceID, const UUID& sceneAsset) {

	Command command{};
	command.kind = CommandKind::LoadSceneAdditive;
	command.sceneInstanceID = sceneInstanceID;
	command.assetID = sceneAsset;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueUnloadScene(const UUID& sceneInstanceID) {

	Command command{};
	command.kind = CommandKind::UnloadScene;
	command.sceneInstanceID = sceneInstanceID;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueLoadSceneSingle(const UUID& sceneInstanceID, const UUID& sceneAsset) {

	Command command{};
	command.kind = CommandKind::LoadSceneSingle;
	command.sceneInstanceID = sceneInstanceID;
	command.assetID = sceneAsset;
	commands_.emplace_back(std::move(command));
}

Engine::WorldCommandBuffer::Command* Engine::WorldCommandBuffer::FindPendingCreateCommand(const Entity& reserved) {

	auto it = createCommandIndex_.find(EntityKey(reserved));
	if (it == createCommandIndex_.end() || commands_.size() <= it->second) {
		return nullptr;
	}
	// indexのコマンドが目的の予約Entityと種別か念のため再確認する
	Command& command = commands_[it->second];
	if ((command.kind == CommandKind::CreateEntity || command.kind == CommandKind::InstantiatePrefab)
		&& command.target == reserved) {
		return &command;
	}
	return nullptr;
}

const Engine::WorldCommandBuffer::Command* Engine::WorldCommandBuffer::FindPendingCreateCommand(const Entity& reserved) const {

	auto it = createCommandIndex_.find(EntityKey(reserved));
	if (it == createCommandIndex_.end() || commands_.size() <= it->second) {
		return nullptr;
	}
	// indexのコマンドが目的の予約Entityと種別か念のため再確認する
	const Command& command = commands_[it->second];
	if ((command.kind == CommandKind::CreateEntity || command.kind == CommandKind::InstantiatePrefab)
		&& command.target == reserved) {
		return &command;
	}
	return nullptr;
}

bool Engine::WorldCommandBuffer::IsPendingCreate(const Entity& reserved) const {

	return FindPendingCreateCommand(reserved) != nullptr;
}

bool Engine::WorldCommandBuffer::StageCreatePosition(const Entity& reserved, const Vector3& position) {

	Command* command = FindPendingCreateCommand(reserved);
	if (!command) {
		return false;
	}
	command->position = position;
	command->flags |= FlagHasPosition;
	return true;
}

bool Engine::WorldCommandBuffer::StageCreateRotation(const Entity& reserved, const Quaternion& rotation) {

	Command* command = FindPendingCreateCommand(reserved);
	if (!command) {
		return false;
	}
	command->rotation = rotation;
	command->flags |= FlagHasRotation;
	return true;
}

bool Engine::WorldCommandBuffer::StageCreateScale(const Entity& reserved, const Vector3& scale) {

	Command* command = FindPendingCreateCommand(reserved);
	if (!command) {
		return false;
	}
	command->scale = scale;
	command->flags |= FlagHasScale;
	return true;
}

void Engine::WorldCommandBuffer::Flush(ECSWorld& world) {

	// 再入時はネストせず、外側のbatchループに任せる
	if (flushing_) {
		return;
	}
	flushing_ = true;

	int32_t batchCount = 0;
	while (!commands_.empty()) {

		// 上限を超えたら残りは次フレームのFlushへ回す、破棄はしない
		if (kMaxFlushBatches <= batchCount) {

			Logger::Output(LogType::Engine, spdlog::level::warn,
				"WorldCommandBuffer: exceeded max flush batches. deferring {} commands to next flush.",
				commands_.size());
			break;
		}

		// 現batchを切り離してから適用する、適用中に積まれた分は次batchへ回る
		std::vector<Command> batch;
		batch.swap(commands_);
		// commands_を切り離したのでindex mapも無効化する
		createCommandIndex_.clear();
		for (const Command& command : batch) {

			Apply(world, command);
		}
		++batchCount;
	}

	flushing_ = false;
}

void Engine::WorldCommandBuffer::Clear() {

	commands_.clear();
	createCommandIndex_.clear();
}

void Engine::WorldCommandBuffer::Apply(ECSWorld& world, const Command& command) {

	// Sceneコマンドはtarget Entityを持たないため、IsAlive検証より前に処理する
	if (command.kind == CommandKind::LoadSceneAdditive || command.kind == CommandKind::LoadSceneSingle ||
		command.kind == CommandKind::UnloadScene) {

		const WorldCommandServices& services = world.GetCommandServices();
		if (!services.sceneInstances || !services.assetDatabase || !services.sceneSystem) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"WorldCommandBuffer: scene command skipped (command services not set on world).");
			return;
		}
		if (command.kind == CommandKind::LoadSceneAdditive) {
			services.sceneInstances->LoadAdditive(*services.assetDatabase, *services.sceneSystem, world,
				AssetID{ command.assetID }, command.sceneInstanceID);
		} else if (command.kind == CommandKind::LoadSceneSingle) {

			// 単一ロード、UnityのadditiveでないLoadScene相当
			// 新sceneをloadする前に現在ロード中のsceneIDを退避し、新sceneをactiveにしてから旧sceneを全てunloadする
			std::vector<UUID> previousScenes;
			previousScenes.reserve(services.sceneInstances->GetAll().size());
			for (const SceneInstance& scene : services.sceneInstances->GetAll()) {
				previousScenes.emplace_back(scene.instanceID);
			}
			if (!services.sceneInstances->LoadAdditive(*services.assetDatabase,
				*services.sceneSystem, world, AssetID{ command.assetID },
				command.sceneInstanceID)) {

				services.sceneInstances->ClearSingleLoadRequest();
				return;
			}
			services.sceneInstances->SetActive(command.sceneInstanceID);
			// 新scene以外の旧sceneを全てunloadする
			for (const UUID& previous : previousScenes) {
				if (previous != command.sceneInstanceID) {
					services.sceneInstances->Unload(world, previous);
				}
			}
		} else {
			services.sceneInstances->Unload(world, command.sceneInstanceID);
		}
		return;
	}

	// 積まれてから破棄された可能性があるため、適用直前に必ず再検証する
	if (!world.IsAlive(command.target)) {
		return;
	}

	switch (command.kind) {
	case CommandKind::DestroyEntity:

		// 同一エンティティへの重複Destroyはpendingで安全に無視される
		DestroyEntitySubtree(world, command.target);
		break;
	case CommandKind::AddComponentByName:

		// 同一componentのAdd/Removeが混在しても、enqueue順(=呼び出し順)で決定的に適用する
		world.AddComponentByName(command.target, command.text);
		break;
	case CommandKind::RemoveComponentByName:

		world.RemoveComponentByName(command.target, command.text);
		break;
	case CommandKind::SetNameEnsuringComponent: {

		NameComponent* nameComponent = world.TryGetComponent<NameComponent>(command.target);
		if (!nameComponent) {
			nameComponent = &world.AddComponent<NameComponent>(command.target);
		}
		nameComponent->name = command.text;
		break;
	}
	case CommandKind::SetActiveSelfEnsuringComponent: {

		SceneObjectComponent& sceneObject = SceneObjectUtility::EnsureSceneObject(world, command.target);
		sceneObject.activeSelf = command.boolValue;
		// アクティブ状態を親子階層全体へ伝播させる
		HierarchySystem hierarchySystem{};
		hierarchySystem.UpdateActiveInHierarchy(world, command.target);
		break;
	}
	case CommandKind::SetParent: {

		// 親が破棄済みならルート化する
		Entity parent = world.IsAlive(command.parent) ? command.parent : Entity::Null();
		// 循環を作る付け替えは拒否する、childがparentの祖先になるケース
		if (world.IsAlive(parent) && WouldCreateCycle(world, command.target, parent)) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"WorldCommandBuffer: SetParent rejected (would create hierarchy cycle).");
			break;
		}

		// worldPositionStays:付け替え前のworld姿勢を控えておき、付け替え後にlocalへ落として復元する
		ResolvedWorldTransform worldBefore{};
		bool keepWorld = command.boolValue;
		if (keepWorld) {
			keepWorld = TransformWorldUtility::ResolveWorldTransform(world, command.target, worldBefore);
		}

		HierarchySystem hierarchySystem{};
		hierarchySystem.SetParent(world, command.target, parent);

		if (keepWorld) {
			PreserveWorldTransform(world, command.target, worldBefore);
		}
		break;
	}
	case CommandKind::CreateEntity: {

		// 予約済みの空EntityをGameObjectとしてmaterializeしTransform/SceneObject/Nameを付与する
		SceneAuthoring::EnsureGameObjectDefaults(world, command.target);
		// 生成EntityをsceneInstanceへ所属させる、InstantiatePrefabと同様に未設定だと描画フィルタで除外される
		const WorldCommandServices& services = world.GetCommandServices();
		if (SceneObjectComponent* sceneObject = world.TryGetComponent<SceneObjectComponent>(command.target)) {
			if (world.IsAlive(command.parent)) {
				if (const SceneObjectComponent* parentSceneObject = world.TryGetComponent<SceneObjectComponent>(command.parent)) {
					sceneObject->sceneInstanceID = parentSceneObject->sceneInstanceID;
				}
			}
			if (!sceneObject->sceneInstanceID && services.sceneInstances) {
				if (const SceneInstance* activeScene = services.sceneInstances->GetActive()) {
					sceneObject->sceneInstanceID = activeScene->instanceID;
				}
			}
		}
		if (!command.text.empty()) {
			NameComponent* nameComponent = world.TryGetComponent<NameComponent>(command.target);
			if (!nameComponent) {
				nameComponent = &world.AddComponent<NameComponent>(command.target);
			}
			nameComponent->name = command.text;
		}
		// callback中にstagingされた初期SRTを適用する、pending中のTransform書き込み
		if (TransformComponent* transform = world.TryGetComponent<TransformComponent>(command.target)) {
			if (command.flags & FlagHasPosition) {
				transform->localPos = command.position;
			}
			if (command.flags & FlagHasRotation) {
				transform->localRotation = Quaternion::Normalize(command.rotation);
			}
			if (command.flags & FlagHasScale) {
				transform->localScale = command.scale;
			}
		}
		// 親付けはTransform確定後に行う
		if (world.IsAlive(command.parent) && !WouldCreateCycle(world, command.target, command.parent)) {
			HierarchySystem hierarchySystem{};
			hierarchySystem.SetParent(world, command.target, command.parent);
		}
		break;
	}
	case CommandKind::InstantiatePrefab: {

		const WorldCommandServices& services = world.GetCommandServices();
		if (!services.assetDatabase) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"WorldCommandBuffer: InstantiatePrefab skipped (command services not set on world).");
			break;
		}
		HierarchySystem hierarchySystem{};
		PrefabSystem prefabSystem{};
		PrefabInstantiateResult result{};
		PrefabInstantiateDesc desc{};
		desc.parent = world.IsAlive(command.parent) ? command.parent : Entity::Null();
		// 予約済みルートをPrefabSystemのルートとして使わせる、deferredでも実root handleを返せるようにする
		desc.reservedRoot = command.target;
		// 生成インスタンスをsceneInstanceへ所属させる、未設定だとsceneInstanceIDが空のままになり
		// RenderPassItemCollectorのscene振り分けで除外されてDrawが発行されない
		// 親があれば親のsceneを継ぎ、無ければアクティブsceneへ所属させる
		if (world.IsAlive(desc.parent)) {
			if (const SceneObjectComponent* parentSceneObject = world.TryGetComponent<SceneObjectComponent>(desc.parent)) {
				desc.ownerSceneInstanceID = parentSceneObject->sceneInstanceID;
			}
		}
		if (!desc.ownerSceneInstanceID && services.sceneInstances) {
			if (const SceneInstance* activeScene = services.sceneInstances->GetActive()) {
				desc.ownerSceneInstanceID = activeScene->instanceID;
			}
		}
		if (!prefabSystem.InstantiatePrefab(*services.assetDatabase, hierarchySystem, world,
			AssetID{ command.assetID }, result, desc)) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"WorldCommandBuffer: InstantiatePrefab failed (missing or invalid prefab).");
			break;
		}
		// 初期transformの適用で指定された場合のみrootのlocal SRTを上書きする
		if ((command.flags & FlagUseTransform) && world.IsAlive(result.root)) {
			if (TransformComponent* transform = world.TryGetComponent<TransformComponent>(result.root)) {
				transform->localPos = command.position;
				transform->localRotation = Quaternion::Normalize(command.rotation);
			}
		}
		break;
	}
	}
}
