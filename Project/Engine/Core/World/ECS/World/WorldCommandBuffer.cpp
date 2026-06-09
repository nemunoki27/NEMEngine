#include "WorldCommandBuffer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

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

void Engine::WorldCommandBuffer::EnqueueSetParent(const Entity& child, const Entity& parent) {

	Command command{};
	command.kind = CommandKind::SetParent;
	command.target = child;
	command.parent = parent;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::Flush(ECSWorld& world) {

	// 再入時はネストせず、外側のbatchループに任せる
	if (flushing_) {
		return;
	}
	flushing_ = true;

	int32_t batchCount = 0;
	while (!commands_.empty()) {

		// 上限を超えたら残りは次フレームのFlushへ回す（破棄はしない）
		if (kMaxFlushBatches <= batchCount) {

			Logger::Output(LogType::Engine, spdlog::level::warn,
				"WorldCommandBuffer: exceeded max flush batches. deferring {} commands to next flush.",
				commands_.size());
			break;
		}

		// 現batchを切り離してから適用する。適用中に積まれた分は次batchへ回る
		std::vector<Command> batch;
		batch.swap(commands_);
		for (const Command& command : batch) {

			Apply(world, command);
		}
		++batchCount;
	}

	flushing_ = false;
}

void Engine::WorldCommandBuffer::Clear() {

	commands_.clear();
}

void Engine::WorldCommandBuffer::Apply(ECSWorld& world, const Command& command) {

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
		const Entity parent = world.IsAlive(command.parent) ? command.parent : Entity::Null();
		HierarchySystem hierarchySystem{};
		hierarchySystem.SetParent(world, command.target, parent);
		break;
	}
	}
}
