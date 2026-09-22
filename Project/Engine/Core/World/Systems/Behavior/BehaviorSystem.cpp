#include "BehaviorSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>

//============================================================================
//	BehaviorSystem classMethods
//============================================================================

Engine::BehaviorSystem* Engine::BehaviorSystem::activeSystem_ = nullptr;

void Engine::BehaviorSystem::OnWorldEnter(ECSWorld& world, SystemContext& context) {

	activeSystem_ = this;
	session_.participantCache_.lateUpdateParticipants_.clear();
	// ワールドをアクティブにする
	session_.EnsureActiveWorld(world, context);
	if (session_.activeWorld_ == &world && session_.componentMutationListenerID_ == 0) {
		session_.componentMutationListenerID_ = world.AddComponentMutationListener(
			&BehaviorExecutionSession::OnComponentMutation, &session_);
	}

	// プレイモードでワールドに入ったときは、スクリプトのビヘイビアの実体化と初期化を行う
	if (context.mode == WorldMode::Play) {
		session_.SynchronizeLifecycle(world, context, false);
	}
}

void Engine::BehaviorSystem::OnWorldExit(ECSWorld& world, SystemContext& context) {

	// ワールドがアクティブでないなら何もしない
	if (session_.activeWorld_ != &world) {
		return;
	}
	// ワールドを破棄する
	session_.runtime_.DestroyAll(world, context);
	world.RemoveComponentMutationListener(session_.componentMutationListenerID_);
	session_.componentMutationListenerID_ = 0;
	session_.activeWorld_ = nullptr;
	session_.participantCache_.lateUpdateParticipants_.clear();
	if (activeSystem_ == this) {
		activeSystem_ = nullptr;
	}
}

void Engine::BehaviorSystem::FixedUpdate(ECSWorld& world, SystemContext& context) {

	// プレイモード以外は処理しない
	if (context.mode != WorldMode::Play) {
		return;
	}

	// fixedステップごとにライフサイクルを同期してから更新する
	session_.SynchronizeLifecycle(world, context, false);

	// コールバック中のScript追加で元の配列が変わっても反復を継続できるようにする
	const std::vector<BehaviorParticipantCache::SyncParticipant> participants = session_.participantCache_.participants_;
	for (const BehaviorParticipantCache::SyncParticipant& participant : participants) {

		BehaviorRecord* record = session_.runtime_.GetRecord(participant.handle);
		if (!record || !session_.CanInvokeParticipant(world, participant, *record)) {
			continue;
		}
		record->instance->FixedUpdate(world, context, record->owner);
		session_.RefreshFaultState(participant.handle);
		session_.SynchronizeLifecycleIfDirty(world, context);
	}

	// FixedUpdate末でWaitForFixedUpdateのコルーチンをresumeする
	ManagedScriptRuntime::GetInstance().TickFrame(1, context);
	session_.SynchronizeLifecycleIfDirty(world, context);
}

void Engine::BehaviorSystem::Update(ECSWorld& world, SystemContext& context) {

	// プレイモード以外は処理しない
	if (context.mode != WorldMode::Play) {
		return;
	}
	session_.participantCache_.lateUpdateParticipants_.clear();

	// Update前にライフサイクルを同期し、不要なビヘイビアをsweepする
	session_.SynchronizeLifecycle(world, context, true);

	// コールバック中のScript追加で元の配列が変わっても反復を継続できるようにする
	const std::vector<BehaviorParticipantCache::SyncParticipant> participants = session_.participantCache_.participants_;
	for (const BehaviorParticipantCache::SyncParticipant& participant : participants) {

		BehaviorRecord* record = session_.runtime_.GetRecord(participant.handle);
		if (!record || !session_.CanInvokeParticipant(world, participant, *record)) {
			continue;
		}
		session_.participantCache_.lateUpdateParticipants_.emplace_back(participant);
		record->instance->Update(world, context, record->owner);
		session_.RefreshFaultState(participant.handle);
		session_.SynchronizeLifecycleIfDirty(world, context);
	}

	// Update末でTimerとコルーチンを駆動する
	ManagedScriptRuntime::GetInstance().TickFrame(0, context);
	session_.SynchronizeLifecycleIfDirty(world, context);
}

void Engine::BehaviorSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// プレイモード以外、もしくはアクティブワールドでないときは処理しない
	if (context.mode != WorldMode::Play || session_.activeWorld_ != &world) {
		return;
	}
	// LateUpdateは同じフレームでUpdateを実行したparticipantだけに呼ぶ
	for (const BehaviorParticipantCache::SyncParticipant& participant : session_.participantCache_.lateUpdateParticipants_) {

		BehaviorRecord* record = session_.runtime_.GetRecord(participant.handle);
		if (!record || !session_.CanInvokeParticipant(world, participant, *record)) {
			continue;
		}
		record->instance->LateUpdate(world, context, record->owner);
		session_.RefreshFaultState(participant.handle);
		session_.SynchronizeLifecycleIfDirty(world, context);
	}

	// LateUpdate末でWaitForEndOfFrameのコルーチンをresumeする
	ManagedScriptRuntime::GetInstance().TickFrame(2, context);
	session_.SynchronizeLifecycleIfDirty(world, context);
}

void Engine::BehaviorSystem::OnSceneInstancesChanged(ECSWorld& world,
	SystemContext& context, [[maybe_unused]] SceneChangePhase phase) {

	if (context.mode != WorldMode::Play || session_.activeWorld_ != &world) {
		return;
	}

	// 新しいシーンの最初の描画前にAwake、OnEnable、Startを確定する
	session_.fullSyncRequested_ = true;
	session_.SynchronizeLifecycle(world, context, true);
}
