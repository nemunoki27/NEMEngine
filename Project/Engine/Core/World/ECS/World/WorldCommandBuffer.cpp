#include "WorldCommandBuffer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/WorldCommandExecutor.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	WorldCommandBuffer classMethods
//============================================================================
void Engine::WorldCommandBuffer::EnqueueDestroyEntity(const Entity& entity) {

	WorldCommand command{};
	command.kind = WorldCommandKind::DestroyEntity;
	command.target = entity;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueAddComponentByName(const Entity& entity, std::string_view typeName) {

	WorldCommand command{};
	command.kind = WorldCommandKind::AddComponentByName;
	command.target = entity;
	command.text.assign(typeName);
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueRemoveComponentByName(const Entity& entity, std::string_view typeName) {

	WorldCommand command{};
	command.kind = WorldCommandKind::RemoveComponentByName;
	command.target = entity;
	command.text.assign(typeName);
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueSetNameEnsuringComponent(const Entity& entity, std::string_view name) {

	WorldCommand command{};
	command.kind = WorldCommandKind::SetNameEnsuringComponent;
	command.target = entity;
	command.text.assign(name);
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueSetActiveSelfEnsuringComponent(const Entity& entity, bool active) {

	WorldCommand command{};
	command.kind = WorldCommandKind::SetActiveSelfEnsuringComponent;
	command.target = entity;
	command.boolValue = active;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueSetParent(const Entity& child, const Entity& parent, bool worldPositionStays) {

	WorldCommand command{};
	command.kind = WorldCommandKind::SetParent;
	command.target = child;
	command.parent = parent;
	command.boolValue = worldPositionStays;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueCreateEntity(const Entity& reserved, std::string_view name, const Entity& parent) {

	WorldCommand command{};
	command.kind = WorldCommandKind::CreateEntity;
	command.target = reserved;
	command.parent = parent;
	command.text.assign(name);
	commands_.emplace_back(std::move(command));
	// 予約Entityからこのコマンドのindexを引けるよう記録する
	createCommandIndex_[EntityKey(reserved)] = commands_.size() - 1;
}

void Engine::WorldCommandBuffer::EnqueueLoadSceneAdditive(const UUID& sceneInstanceID, AssetID sceneAsset) {

	WorldCommand command{};
	command.kind = WorldCommandKind::LoadSceneAdditive;
	command.sceneInstanceID = sceneInstanceID;
	command.assetID = sceneAsset;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueUnloadScene(const UUID& sceneInstanceID) {

	WorldCommand command{};
	command.kind = WorldCommandKind::UnloadScene;
	command.sceneInstanceID = sceneInstanceID;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueLoadSceneSingle(const UUID& sceneInstanceID, AssetID sceneAsset) {

	WorldCommand command{};
	command.kind = WorldCommandKind::LoadSceneSingle;
	command.sceneInstanceID = sceneInstanceID;
	command.assetID = sceneAsset;
	commands_.emplace_back(std::move(command));
}

Engine::WorldCommand* Engine::WorldCommandBuffer::FindPendingCreateCommand(const Entity& reserved) {

	auto it = createCommandIndex_.find(EntityKey(reserved));
	if (it == createCommandIndex_.end() || commands_.size() <= it->second) {
		return nullptr;
	}
	// indexのコマンドが目的の予約Entityと種別か念のため再確認する
	WorldCommand& command = commands_[it->second];
	if (command.kind == WorldCommandKind::CreateEntity && command.target == reserved) {
		return &command;
	}
	return nullptr;
}

const Engine::WorldCommand* Engine::WorldCommandBuffer::FindPendingCreateCommand(const Entity& reserved) const {

	auto it = createCommandIndex_.find(EntityKey(reserved));
	if (it == createCommandIndex_.end() || commands_.size() <= it->second) {
		return nullptr;
	}
	// indexのコマンドが目的の予約Entityと種別か念のため再確認する
	const WorldCommand& command = commands_[it->second];
	if (command.kind == WorldCommandKind::CreateEntity && command.target == reserved) {
		return &command;
	}
	return nullptr;
}

bool Engine::WorldCommandBuffer::IsPendingCreate(const Entity& reserved) const {

	return FindPendingCreateCommand(reserved) != nullptr;
}

bool Engine::WorldCommandBuffer::StageCreatePosition(const Entity& reserved, const Vector3& position) {

	WorldCommand* command = FindPendingCreateCommand(reserved);
	if (!command) {
		return false;
	}
	command->position = position;
	command->flags |= FlagHasPosition;
	return true;
}

bool Engine::WorldCommandBuffer::StageCreateRotation(const Entity& reserved, const Quaternion& rotation) {

	WorldCommand* command = FindPendingCreateCommand(reserved);
	if (!command) {
		return false;
	}
	command->rotation = rotation;
	command->flags |= FlagHasRotation;
	return true;
}

bool Engine::WorldCommandBuffer::StageCreateScale(const Entity& reserved, const Vector3& scale) {

	WorldCommand* command = FindPendingCreateCommand(reserved);
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
				"WorldCommandBuffer: 1回の反映上限を超えたため{}件のCommandを次回へ延期します",
				commands_.size());
			break;
		}

		// 現batchを切り離してから適用する、適用中に積まれた分は次batchへ回る
		std::vector<WorldCommand> batch;
		batch.swap(commands_);
		// commands_を切り離したのでindex mapも無効化する
		createCommandIndex_.clear();
		for (const WorldCommand& command : batch) {

			WorldCommandExecutor::Apply(world, command);
		}
		++batchCount;
	}

	flushing_ = false;
}

void Engine::WorldCommandBuffer::Clear() {

	commands_.clear();
	createCommandIndex_.clear();
}
