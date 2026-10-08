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

	// 破棄対象を予約する
	WorldCommand command{};
	command.kind = WorldCommandKind::DestroyEntity;
	command.target = entity;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueAddComponentByName(const Entity& entity, std::string_view typeName) {

	// 型名を保持して追加を予約する
	WorldCommand command{};
	command.kind = WorldCommandKind::AddComponentByName;
	command.target = entity;
	command.text.assign(typeName);
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueRemoveComponentByName(const Entity& entity, std::string_view typeName) {

	// 型名を保持して削除を予約する
	WorldCommand command{};
	command.kind = WorldCommandKind::RemoveComponentByName;
	command.target = entity;
	command.text.assign(typeName);
	commands_.emplace_back(std::move(command));

	// 削除予約の成功後に、未適用の追加を取り消す
	const auto* info = ComponentTypeRegistry::GetInstance().FindByName(typeName);
	if (info) {
		CancelPendingComponent(entity, info->id);
	}
}

void Engine::WorldCommandBuffer::EnqueueRemoveScript(const Entity& entity, const UUID& scriptSlotID) {

	// 保存slotでScriptの削除を予約する
	WorldCommand command{};
	command.kind = WorldCommandKind::RemoveScript;
	command.target = entity;
	command.scriptSlotID = scriptSlotID;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueSetNameEnsuringComponent(const Entity& entity, std::string_view name) {

	// 設定する名前を予約へコピーする
	WorldCommand command{};
	command.kind = WorldCommandKind::SetNameEnsuringComponent;
	command.target = entity;
	command.text.assign(name);
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueSetActiveSelfEnsuringComponent(const Entity& entity, bool active) {

	// 設定する有効状態を保持する
	WorldCommand command{};
	command.kind = WorldCommandKind::SetActiveSelfEnsuringComponent;
	command.target = entity;
	command.boolValue = active;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueSetParent(const Entity& child, const Entity& parent, bool worldPositionStays) {

	// 親とWorld姿勢の維持を予約する
	WorldCommand command{};
	command.kind = WorldCommandKind::SetParent;
	command.target = child;
	command.parent = parent;
	command.boolValue = worldPositionStays;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueLoadSceneAdditive(const UUID& sceneInstanceID, AssetID sceneAsset) {

	// Sceneと先行採番したInstanceを保持する
	WorldCommand command{};
	command.kind = WorldCommandKind::LoadSceneAdditive;
	command.sceneInstanceID = sceneInstanceID;
	command.assetID = sceneAsset;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueUnloadScene(const UUID& sceneInstanceID) {

	// 解放するScene Instanceを保持する
	WorldCommand command{};
	command.kind = WorldCommandKind::UnloadScene;
	command.sceneInstanceID = sceneInstanceID;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::EnqueueLoadSceneSingle(const UUID& sceneInstanceID, AssetID sceneAsset) {

	// 切り替え先のScene Instanceを保持する
	WorldCommand command{};
	command.kind = WorldCommandKind::LoadSceneSingle;
	command.sceneInstanceID = sceneInstanceID;
	command.assetID = sceneAsset;
	commands_.emplace_back(std::move(command));
}

void Engine::WorldCommandBuffer::Flush(ECSWorld& world) {

	// 再入した予約は外側のFlushで適用する
	if (flushing_ || world.IsStructuralChangeDeferred()) {
		return;
	}
	const auto lifetime = world.GetLifetime();
	const bool ownedByWorld = &world.GetCommandBuffer() == this;
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
			lifetime->ThrowIfEnded();
		}
	} catch (...) {
		// Worldとともに終了したCommandBufferへ戻らない
		if (!ownedByWorld || lifetime->IsAlive()) {
			flushing_ = false;
		}
		throw;
	}
	flushing_ = false;
}

void Engine::WorldCommandBuffer::Clear() {

	// 予約値と適用中のbatchを解放する
	pendingComponents_.clear();
	activeBatch_.clear();
	activeCommandIndex_ = 0;
	commands_.clear();
}
