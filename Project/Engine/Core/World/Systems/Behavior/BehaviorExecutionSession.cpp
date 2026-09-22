#include "BehaviorExecutionSession.h"

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


namespace {
	constexpr uint32_t kMaxLifecycleTransitionPassCount = 64;
}

void Engine::BehaviorExecutionSession::EnsureActiveWorld(ECSWorld& world, SystemContext& context) {

	// プレイ中でないときにアクティブにしない
	if (context.mode != WorldMode::Play) {
		activeWorld_ = nullptr;
		return;
	}
	// すでにアクティブなワールドなら何もしない
	if (activeWorld_ == &world) {
		return;
	}

	// 前のワールドを破棄する
	if (activeWorld_) {

		activeWorld_->RemoveComponentMutationListener(componentMutationListenerID_);
		componentMutationListenerID_ = 0;
		runtime_.DestroyAll(*activeWorld_, context);
	}

	// 新しいワールドをアクティブにする
	activeWorld_ = &world;
	ResetRuntimeState(world);
	componentMutationListenerID_ = world.AddComponentMutationListener(
		&BehaviorExecutionSession::OnComponentMutation, this);
}

void Engine::BehaviorExecutionSession::ResetRuntimeState([[maybe_unused]] ECSWorld& world) {

	// participantキャッシュを破棄し、次のsynchronizeで作り直す
	participantCache_.participants_.clear();
	participantCache_.participantsDirty_ = true;
	participantCache_.executionOrderRevision_ = 0;
	dirtyScriptEntities_.clear();
	enableTransitionsDirty_ = true;
	fullSyncRequested_ = true;
	// 実行時対応はBehaviorWorldが所有し、Script設定側へキャッシュしない
}

void Engine::BehaviorExecutionSession::SynchronizeLifecycle(ECSWorld& world, SystemContext& context, bool sweep) {

	// ワールドを設定
	EnsureActiveWorld(world, context);
	// プレイモードでない、もしくはアクティブなワールドでないときは何もしない
	if (context.mode != WorldMode::Play || activeWorld_ != &world) {
		return;
	}

	// 実行順設定が変わった場合はScript構造が同じでも並びを更新する
	if (participantCache_.executionOrderRevision_ !=
		BehaviorTypeRegistry::GetInstance().GetExecutionOrderRevision()) {

		participantCache_.participantsDirty_ = true;
	}

	// 初回とHot Reloadだけ全走査し、通常フレームは変更されたEntityだけ同期する
	if (fullSyncRequested_) {
		dirtyScriptEntities_.clear();
		records_.SynchronizeRecords(world, context, sweep);
		fullSyncRequested_ = false;
	} else {
		records_.SynchronizeDirtyRecords(world, context);
	}

	// 構造変更時だけparticipantキャッシュを作り直して安定ソートする
	if (participantCache_.participantsDirty_) {

		participantCache_.RebuildParticipants(world, runtime_);
		participantCache_.participantsDirty_ = false;
	}

	// ScriptかActive状態が変わった場合だけライフサイクル遷移を再評価する
	FlushActiveTransitions(world, context);
	// Pass4 全Awake/OnEnable後Startより前にSceneLoaded/Unloadedを発火する
	ManagedScriptRuntime::GetInstance().PumpSceneEvents();
	FlushActiveTransitions(world, context);

	// Start中に先行順のScriptが有効化された場合も同じ同期内で開始する
	for (uint32_t pass = 0; pass < kMaxLifecycleTransitionPassCount; ++pass) {

		if (!InvokePendingStart(world, context)) {
			break;
		}
	}
}

void Engine::BehaviorExecutionSession::OnComponentMutation(ECSWorld& world, const Entity& entity,
	uint32_t typeID, ComponentMutationKind kind, void* userData) {

	auto* system = static_cast<BehaviorExecutionSession*>(userData);
	if (!system || system->activeWorld_ != &world) {
		return;
	}

	if (kind == ComponentMutationKind::EntityDestroyed) {
		system->records_.QueueScriptEntity(entity);
		system->enableTransitionsDirty_ = true;
		return;
	}

	ComponentTypeRegistry& registry = ComponentTypeRegistry::GetInstance();
	if (typeID == registry.GetID<ScriptComponent>() ||
		typeID == registry.GetID<ScriptEntry>()) {
		system->records_.QueueScriptEntity(entity);
		system->participantCache_.participantsDirty_ = true;
		return;
	}
	if (typeID == registry.GetID<SceneObjectComponent>() ||
		typeID == registry.GetID<HierarchyComponent>()) {
		system->enableTransitionsDirty_ = true;
	}
}

void Engine::BehaviorExecutionSession::InvokePendingAwake(ECSWorld& world, SystemContext& context) {

	// activeかつ未AwakeのrecordにAwakeを1回呼ぶ
	const std::vector<SyncParticipant> participants = participantCache_.participants_;
	for (const SyncParticipant& participant : participants) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || record->faulted || record->awakeCalled) {
			continue;
		}
		// inactive hierarchyの間はAwakeを遅延する
		if (!IsEntityActiveInHierarchy(world, participant.owner)) {
			continue;
		}
		record->awakeCalled = true;
		record->instance->Awake(world, context, participant.owner);
		RefreshFaultState(participant.handle);
	}
}

bool Engine::BehaviorExecutionSession::IsParticipantEnabled(ECSWorld& world,
	const SyncParticipant& participant, const BehaviorRecord& record) const {

	if (!world.HasComponent<ScriptComponent>(record.owner) ||
		participant.slot < 0) {
		return false;
	}
	const std::span<const ScriptEntry> entries =
		GetScriptEntries(world, record.owner);
	if (entries.size() <= static_cast<size_t>(participant.slot)) {
		return false;
	}
	if (entries[participant.slot].scriptSlotID != record.scriptSlotID) {
		return false;
	}
	if (record.hasRuntimeEnabledOverride) {
		return record.runtimeEnabledOverride;
	}
	return entries[participant.slot].enabled;
}

bool Engine::BehaviorExecutionSession::CanInvokeParticipant(ECSWorld& world,
	const SyncParticipant& participant, const BehaviorRecord& record) const {

	return record.instance && !record.faulted && record.enabled &&
		world.IsAlive(record.owner) && record.owner == participant.owner &&
		IsEntityActiveInHierarchy(world, record.owner) &&
		IsParticipantEnabled(world, participant, record);
}

void Engine::BehaviorExecutionSession::RefreshFaultState(const BehaviorHandle& handle) {

	BehaviorRecord* record = runtime_.GetRecord(handle);
	if (record && record->instance && record->instance->IsFaulted()) {
		record->faulted = true;
	}
}

void Engine::BehaviorExecutionSession::FlushActiveTransitions(ECSWorld& world, SystemContext& context) {

	uint32_t pass = 0;
	while (enableTransitionsDirty_ && pass < kMaxLifecycleTransitionPassCount) {

		enableTransitionsDirty_ = false;
		InvokePendingAwake(world, context);
		ApplyEnableTransitions(world, context);
		++pass;
	}
	if (enableTransitionsDirty_) {
		enableTransitionsDirty_ = false;
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ScriptのActive状態が収束しませんでした OnEnableまたはOnDisable内のActive変更を確認してください");
	}
}

void Engine::BehaviorExecutionSession::SynchronizeLifecycleIfDirty(ECSWorld& world, SystemContext& context) {

	const bool executionOrderChanged = participantCache_.executionOrderRevision_ !=
		BehaviorTypeRegistry::GetInstance().GetExecutionOrderRevision();
	if (!fullSyncRequested_ && dirtyScriptEntities_.empty() &&
		!participantCache_.participantsDirty_ && !enableTransitionsDirty_ && !executionOrderChanged) {
		return;
	}
	SynchronizeLifecycle(world, context, false);
}

void Engine::BehaviorExecutionSession::ApplyEnableTransitions(ECSWorld& world, SystemContext& context) {

	const std::vector<SyncParticipant> participants = participantCache_.participants_;
	for (const SyncParticipant& participant : participants) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || record->faulted) {
			continue;
		}

		// OnEnableはAwake後かつactiveのときだけ呼ぶ
		const bool shouldBeEnabled =
			IsParticipantEnabled(world, participant, *record) && record->awakeCalled &&
			IsEntityActiveInHierarchy(world, participant.owner);

		if (shouldBeEnabled && !record->enabled) {

			record->enabled = true;
			record->instance->OnEnable(world, context, participant.owner);
		} else if (!shouldBeEnabled && record->enabled) {

			// 有効から無効への遷移時のみOnDisableを呼ぶ
			record->enabled = false;
			record->instance->OnDisable(world, context, participant.owner);
		}
		RefreshFaultState(participant.handle);
	}
}

bool Engine::BehaviorExecutionSession::InvokePendingStart(ECSWorld& world, SystemContext& context) {

	// 有効かつ未StartのrecordにStartを1回呼ぶ、再有効化では再実行しない
	const std::vector<SyncParticipant> participants = participantCache_.participants_;
	bool invoked = false;
	for (const SyncParticipant& participant : participants) {

		FlushActiveTransitions(world, context);
		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !CanInvokeParticipant(world, participant, *record)) {
			continue;
		}
		if (record->awakeCalled && !record->startCalled) {

			record->startCalled = true;
			record->instance->Start(world, context, participant.owner);
			RefreshFaultState(participant.handle);
			invoked = true;
		}
	}
	FlushActiveTransitions(world, context);
	return invoked;
}

void Engine::BehaviorExecutionSession::DispatchCollision(ECSWorld& world,
	SystemContext& context, const CollisionContact& collision, int32_t phase) {

	if (context.mode != WorldMode::Play || activeWorld_ != &world) {
		return;
	}

	// 実行順を保ったスナップショットからContactのselfに一致するScriptだけへ通知する
	const std::vector<SyncParticipant> participants = participantCache_.participants_;
	for (const SyncParticipant& participant : participants) {

		if (participant.owner != collision.self) {
			continue;
		}
		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !CanInvokeParticipant(world, participant, *record)) {
			continue;
		}
		switch (phase) {
		case 0:
			record->instance->OnCollisionEnter(world, context, collision);
			break;
		case 1:
			record->instance->OnCollisionStay(world, context, collision);
			break;
		case 2:
			record->instance->OnCollisionExit(world, context, collision);
			break;
		default:
			break;
		}
		RefreshFaultState(participant.handle);
		SynchronizeLifecycleIfDirty(world, context);
	}
}
