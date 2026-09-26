#include "WorldCommandBuffer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/WorldCommandExecutor.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/PendingComponent.h>
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

	// 削除予約の成功後に、未適用の追加を取り消す
	const auto* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	if (info) {
		const auto entry = pendingComponents_.find({ entity.index, entity.generation, info->id });
		if (entry != pendingComponents_.end()) {
			auto cancelled = std::move(entry->second);
			pendingComponents_.erase(entry);
			cancelled->Cancel();
		}
	}
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

void Engine::WorldCommandBuffer::Flush(ECSWorld& world) {

	// 再入時はネストせず、外側のbatchループに任せる
	if (flushing_ || world.IsStructuralChangeDeferred()) {
		return;
	}
	flushing_ = true;

	int32_t batchCount = 0;
	try {
		while (!IsEmpty()) {
			if (activeCommandIndex_ == activeBatch_.size()) {
				if (batchCount == kMaxFlushBatches) {
					Logger::Output(LogType::Engine, spdlog::level::warn,
						"WorldCommandBuffer: 1回の反映上限を超えたため{}件のCommandを次回へ延期します", commands_.size());
					break;
				}

				// 現在の予約を切り離し、新しい予約と分ける
				activeBatch_.clear();
				activeBatch_.swap(commands_);
				activeCommandIndex_ = 0;
				++batchCount;
			}
			// 失敗したCommandは再実行せず、残りを次回へ保持する
			WorldCommand command = std::move(activeBatch_[activeCommandIndex_++]);
			RemovePendingComponent(command);
			WorldCommandExecutor::Apply(world, command);
		}
	} catch (...) {
		flushing_ = false;
		throw;
	}
	flushing_ = false;
}

void Engine::WorldCommandBuffer::Clear() {

	pendingComponents_.clear();
	activeBatch_.clear();
	activeCommandIndex_ = 0;
	commands_.clear();
}
