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
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

namespace {

	using namespace Engine;

	// 親階層を辿ったworld回転でlocal値の積、worldMatrix分解ではない
	Quaternion WorldRotationOf(ECSWorld& world, const Entity& entity) {

		TransformComponent* self = world.TryGetComponent<TransformComponent>(entity);
		Quaternion rotation = self ? self->localRotation : Quaternion::Identity();
		Entity current = entity;
		for (int32_t guard = 0; guard < 1024; ++guard) {
			HierarchyComponent* hierarchy = world.TryGetComponent<HierarchyComponent>(current);
			if (!hierarchy || !world.IsAlive(hierarchy->parent)) {
				break;
			}
			const Entity parent = hierarchy->parent;
			if (TransformComponent* parentTransform = world.TryGetComponent<TransformComponent>(parent)) {
				rotation = parentTransform->localRotation * rotation;
			}
			current = parent;
		}
		return rotation;
	}

	// 親階層のlocalScaleを成分積で累積したworld(lossy) scale
	Vector3 WorldScaleOf(ECSWorld& world, const Entity& entity) {

		TransformComponent* self = world.TryGetComponent<TransformComponent>(entity);
		Vector3 scale = self ? self->localScale : Vector3::AnyInit(1.0f);
		Entity current = entity;
		for (int32_t guard = 0; guard < 1024; ++guard) {
			HierarchyComponent* hierarchy = world.TryGetComponent<HierarchyComponent>(current);
			if (!hierarchy || !world.IsAlive(hierarchy->parent)) {
				break;
			}
			const Entity parent = hierarchy->parent;
			if (TransformComponent* parentTransform = world.TryGetComponent<TransformComponent>(parent)) {
				const Vector3& parentScale = parentTransform->localScale;
				scale = Vector3(scale.x * parentScale.x, scale.y * parentScale.y, scale.z * parentScale.z);
			}
			current = parent;
		}
		return scale;
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

	// worldPositionStays:親変更前のchild world姿勢を、変更後のnewParent基準のlocal値へ落として維持する
	void PreserveWorldTransform(ECSWorld& world, const Entity& child, const Entity& newParent,
		const Matrix4x4& childWorldBefore, const Quaternion& childWorldRotBefore, const Vector3& childWorldScaleBefore) {

		TransformComponent* childTransform = world.TryGetComponent<TransformComponent>(child);
		if (!childTransform) {
			return;
		}
		const Vector3 worldPos = childWorldBefore.GetTranslationValue();

		if (world.IsAlive(newParent) && world.TryGetComponent<TransformComponent>(newParent)) {

			TransformComponent* parentTransform = world.TryGetComponent<TransformComponent>(newParent);
			const Matrix4x4 inverseParent = Matrix4x4::Inverse(parentTransform->worldMatrix);
			childTransform->localPos = Vector3::TransformPoint(worldPos, inverseParent);

			const Quaternion parentWorldRot = WorldRotationOf(world, newParent);
			childTransform->localRotation = Quaternion::Normalize(Quaternion::Inverse(parentWorldRot) * childWorldRotBefore);

			const Vector3 parentWorldScale = WorldScaleOf(world, newParent);
			childTransform->localScale = Vector3(
				parentWorldScale.x != 0.0f ? childWorldScaleBefore.x / parentWorldScale.x : childWorldScaleBefore.x,
				parentWorldScale.y != 0.0f ? childWorldScaleBefore.y / parentWorldScale.y : childWorldScaleBefore.y,
				parentWorldScale.z != 0.0f ? childWorldScaleBefore.z / parentWorldScale.z : childWorldScaleBefore.z);
		} else {

			// ルート化: world値をそのままlocalとする
			childTransform->localPos = worldPos;
			childTransform->localRotation = Quaternion::Normalize(childWorldRotBefore);
			childTransform->localScale = childWorldScaleBefore;
		}
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
	if (command.kind == CommandKind::LoadSceneAdditive || command.kind == CommandKind::UnloadScene) {

		const WorldCommandServices& services = world.GetCommandServices();
		if (!services.sceneInstances || !services.assetDatabase || !services.sceneSystem) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"WorldCommandBuffer: scene command skipped (command services not set on world).");
			return;
		}
		if (command.kind == CommandKind::LoadSceneAdditive) {
			services.sceneInstances->LoadAdditive(*services.assetDatabase, *services.sceneSystem, world,
				AssetID{ command.assetID }, command.sceneInstanceID);
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
		world.DestroyEntity(command.target);
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
		Matrix4x4 worldBefore = Matrix4x4::Identity();
		Quaternion worldRotBefore = Quaternion::Identity();
		Vector3 worldScaleBefore = Vector3::AnyInit(1.0f);
		const bool keepWorld = command.boolValue;
		if (keepWorld) {
			if (TransformComponent* childTransform = world.TryGetComponent<TransformComponent>(command.target)) {
				worldBefore = childTransform->worldMatrix;
			}
			worldRotBefore = WorldRotationOf(world, command.target);
			worldScaleBefore = WorldScaleOf(world, command.target);
		}

		HierarchySystem hierarchySystem{};
		hierarchySystem.SetParent(world, command.target, parent);

		if (keepWorld) {
			PreserveWorldTransform(world, command.target, parent, worldBefore, worldRotBefore, worldScaleBefore);
		}
		break;
	}
	case CommandKind::CreateEntity: {

		// 予約済みの空EntityをGameObjectとしてmaterializeしTransform/SceneObject/Nameを付与する
		SceneAuthoring::EnsureGameObjectDefaults(world, command.target);
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
