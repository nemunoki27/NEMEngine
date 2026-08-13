#include "BehaviorSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>

// c++
#include <algorithm>

//============================================================================
//	BehaviorSystem classMethods
//============================================================================

Engine::BehaviorSystem* Engine::BehaviorSystem::activeSystem_ = nullptr;

namespace {

	// ScriptEntryをGUID優先でランタイム型IDへ解決する
	bool TryResolveTypeID(Engine::ScriptEntry& entry, uint32_t& outTypeID) {

		auto& registry = Engine::BehaviorTypeRegistry::GetInstance();

		const Engine::BehaviorTypeInfo* info =
			registry.FindByStableScriptTypeID(entry.scriptTypeID);
		if (!info) {
			return false;
		}

		entry.lastKnownTypeName = info->name;
		outTypeID = info->id;
		return true;
	}
}

void Engine::BehaviorSystem::OnWorldEnter(ECSWorld& world, SystemContext& context) {

	activeSystem_ = this;
	lateUpdateParticipants_.clear();
	// ワールドをアクティブにする
	EnsureActiveWorld(world, context);
	if (activeWorld_ == &world && componentMutationListenerID_ == 0) {
		componentMutationListenerID_ = world.AddComponentMutationListener(
			&BehaviorSystem::OnComponentMutation, this);
	}

	// プレイモードでワールドに入ったときは、スクリプトのビヘイビアの実体化と初期化を行う
	if (context.mode == WorldMode::Play) {
		SynchronizeLifecycle(world, context, false);
	}
}

void Engine::BehaviorSystem::OnWorldExit(ECSWorld& world, SystemContext& context) {

	// ワールドがアクティブでないなら何もしない
	if (activeWorld_ != &world) {
		return;
	}
	// ワールドを破棄する
	runtime_.DestroyAll(world, context);
	world.RemoveComponentMutationListener(componentMutationListenerID_);
	componentMutationListenerID_ = 0;
	activeWorld_ = nullptr;
	lateUpdateParticipants_.clear();
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
	SynchronizeLifecycle(world, context, false);

	// 確定済みのparticipantを決定的な順序で実行する
	for (const SyncParticipant& participant : participants_) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || !record->enabled || record->faulted) {
			continue;
		}
		record->instance->FixedUpdate(world, context, record->owner);
		if (record->instance->IsFaulted()) {
			record->faulted = true;
		}
	}

	// FixedUpdate末でWaitForFixedUpdateのコルーチンをresumeする
	ManagedScriptRuntime::GetInstance().TickFrame(1, context);
}

void Engine::BehaviorSystem::Update(ECSWorld& world, SystemContext& context) {

	// プレイモード以外は処理しない
	if (context.mode != WorldMode::Play) {
		return;
	}
	lateUpdateParticipants_.clear();

	// Update前にライフサイクルを同期し、不要なビヘイビアをsweepする
	SynchronizeLifecycle(world, context, true);

	for (const SyncParticipant& participant : participants_) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || !record->enabled || record->faulted) {
			continue;
		}
		lateUpdateParticipants_.emplace_back(participant);
		record->instance->Update(world, context, record->owner);
		if (record->instance->IsFaulted()) {
			record->faulted = true;
		}
	}

	// Update末でTimerとコルーチンを駆動する
	ManagedScriptRuntime::GetInstance().TickFrame(0, context);
}

void Engine::BehaviorSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// プレイモード以外、もしくはアクティブワールドでないときは処理しない
	if (context.mode != WorldMode::Play || activeWorld_ != &world) {
		return;
	}
	// LateUpdateは同じフレームでUpdateを実行したparticipantだけに呼ぶ
	for (const SyncParticipant& participant : lateUpdateParticipants_) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || !record->enabled || record->faulted ||
			!world.IsAlive(record->owner) || !IsEntityActiveInHierarchy(world, record->owner) ||
			!IsParticipantEnabled(world, participant, *record)) {
			continue;
		}
		record->instance->LateUpdate(world, context, record->owner);
		if (record->instance->IsFaulted()) {
			record->faulted = true;
		}
	}

	// LateUpdate末でWaitForEndOfFrameのコルーチンをresumeする
	ManagedScriptRuntime::GetInstance().TickFrame(2, context);
}

void Engine::BehaviorSystem::OnSceneInstancesChanged(ECSWorld& world,
	SystemContext& context, [[maybe_unused]] SceneChangePhase phase) {

	if (context.mode != WorldMode::Play || activeWorld_ != &world) {
		return;
	}

	// 新しいシーンの最初の描画前にAwake、OnEnable、Startを確定する
	fullSyncRequested_ = true;
	SynchronizeLifecycle(world, context, true);
}

void Engine::BehaviorSystem::DispatchCollisionEnter(ECSWorld& world,
	SystemContext& context, const CollisionContact& collision) {

	// アクティブなBehaviorSystemへ衝突開始を渡す
	if (activeSystem_) {
		activeSystem_->DispatchCollision(world, context, collision, 0);
	}
}

void Engine::BehaviorSystem::DispatchCollisionStay(ECSWorld& world,
	SystemContext& context, const CollisionContact& collision) {

	// アクティブなBehaviorSystemへ衝突継続を渡す
	if (activeSystem_) {
		activeSystem_->DispatchCollision(world, context, collision, 1);
	}
}

void Engine::BehaviorSystem::DispatchCollisionExit(ECSWorld& world,
	SystemContext& context, const CollisionContact& collision) {

	// アクティブなBehaviorSystemへ衝突終了を渡す
	if (activeSystem_) {
		activeSystem_->DispatchCollision(world, context, collision, 2);
	}
}

void Engine::BehaviorSystem::DispatchAnimationEvent(ECSWorld& world, SystemContext& context, const Entity& entity,
	const std::string& name, float floatParam, int32_t intParam, const std::string& stringParam) {

	if (!activeSystem_ || context.mode != WorldMode::Play || activeSystem_->activeWorld_ != &world) {
		return;
	}

	// 対象Entityのビヘイビアだけへ通知する
	activeSystem_->runtime_.ForEachAliveByOwner(entity, [&](BehaviorRecord& record) {

		if (!record.enabled || !record.instance || record.faulted) {
			return;
		}
		record.instance->OnAnimationEvent(world, context, entity, name, floatParam, intParam, stringParam);
		if (record.instance->IsFaulted()) {
			record.faulted = true;
		}
		});
}

nlohmann::json Engine::BehaviorSystem::GetRuntimeSerializedState(BehaviorHandle handle) {

	// Play中のinstanceの現在値を返す、無効なら空
	if (!activeSystem_ || !activeSystem_->runtime_.IsAlive(handle)) {
		return nlohmann::json::object();
	}
	BehaviorRecord* record = activeSystem_->runtime_.GetRecord(handle);
	if (!record || !record->instance) {
		return nlohmann::json::object();
	}
	return record->instance->GetRuntimeSerializedState();
}

void Engine::BehaviorSystem::SetRuntimeSerializedField(BehaviorHandle handle,
	const std::string& fieldID, const nlohmann::json& value) {

	if (!activeSystem_ || !activeSystem_->runtime_.IsAlive(handle)) {
		return;
	}
	BehaviorRecord* record = activeSystem_->runtime_.GetRecord(handle);
	if (record && record->instance) {
		record->instance->SetRuntimeSerializedField(*activeSystem_->activeWorld_, fieldID, value);
	}
}

namespace {

	// active worldのowner Entity上でscriptSlotID一致のScriptEntryを探す
	Engine::ScriptEntry* FindScriptEntryBySlot(Engine::ECSWorld& world, const Engine::Entity& owner, const Engine::UUID& slotID) {

		if (!world.HasComponent<Engine::ScriptComponent>(owner)) {
			return nullptr;
		}
		for (Engine::ScriptEntry& entry : GetScriptEntries(world, owner)) {
			if (entry.scriptSlotID == slotID) {
				return &entry;
			}
		}
		return nullptr;
	}
}

int32_t Engine::BehaviorSystem::GetScriptEnabled(const Entity& owner, const UUID& scriptSlotID) {

	if (!activeSystem_ || !activeSystem_->activeWorld_) {
		return -1;
	}
	ScriptEntry* entry = FindScriptEntryBySlot(*activeSystem_->activeWorld_, owner, scriptSlotID);
	if (!entry) {
		return -1;
	}
	// runtime overrideがあればそれを、無ければauthoringのenabledを返す
	const BehaviorHandle handle =
		activeSystem_->runtime_.FindHandleBySlot(owner, scriptSlotID);
	if (BehaviorRecord* record = activeSystem_->runtime_.GetRecord(handle)) {
		if (record->hasRuntimeEnabledOverride) {
			return record->runtimeEnabledOverride ? 1 : 0;
		}
	}
	return entry->enabled ? 1 : 0;
}

void Engine::BehaviorSystem::SetScriptEnabled(const Entity& owner, const UUID& scriptSlotID, bool enabled) {

	if (!activeSystem_ || !activeSystem_->activeWorld_) {
		return;
	}
	ScriptEntry* entry = FindScriptEntryBySlot(*activeSystem_->activeWorld_, owner, scriptSlotID);
	if (!entry) {
		return;
	}
	// runtime overrideだけ立て、authoringのenabledは変更しない、次のsyncで反映される
	const BehaviorHandle handle =
		activeSystem_->runtime_.FindHandleBySlot(owner, scriptSlotID);
	if (BehaviorRecord* record = activeSystem_->runtime_.GetRecord(handle)) {
		record->runtimeEnabledOverride = enabled;
		record->hasRuntimeEnabledOverride = true;
		activeSystem_->enableTransitionsDirty_ = true;
	}
}

Engine::BehaviorHandle Engine::BehaviorSystem::FindRuntimeHandle(
	const Entity& owner, const UUID& scriptSlotID) {

	if (!activeSystem_ || !activeSystem_->activeWorld_) {
		return BehaviorHandle::Null();
	}
	return activeSystem_->runtime_.FindHandleBySlot(owner, scriptSlotID);
}

Engine::MonoBehavior* Engine::BehaviorSystem::FindScriptInstance(const Entity& owner, const std::string& scriptTypeID) {

	if (!activeSystem_ || !activeSystem_->activeWorld_) {
		return nullptr;
	}

	// owner上でscriptTypeID一致のScriptEntryを探しinstanceを返す
	if (!activeSystem_->activeWorld_->HasComponent<ScriptComponent>(owner)) {
		return nullptr;
	}
	for (ScriptEntry& entry :
		GetScriptEntries(*activeSystem_->activeWorld_, owner)) {
		if (entry.scriptTypeID != scriptTypeID) {
			continue;
		}
		const BehaviorHandle handle =
			activeSystem_->runtime_.FindHandleBySlot(owner, entry.scriptSlotID);
		if (BehaviorRecord* record = activeSystem_->runtime_.GetRecord(handle)) {
			if (record->instance) {
				return record->instance.get();
			}
		}
	}
	return nullptr;
}

bool Engine::BehaviorSystem::AttachScript(const Entity& owner, const std::string& scriptTypeID) {

	if (!activeSystem_ || !activeSystem_->activeWorld_ || scriptTypeID.empty()) {
		return false;
	}
	ECSWorld& world = *activeSystem_->activeWorld_;
	if (!world.IsAlive(owner)) {
		return false;
	}

	// 型GUIDから実行時型IDを解決する、未登録なら失敗
	const BehaviorTypeInfo* info = BehaviorTypeRegistry::GetInstance().FindByStableScriptTypeID(scriptTypeID);
	if (!info) {
		return false;
	}

	// ScriptComponentが無ければ付与する、すでにscriptを持つEntityへの追加はvectorへのappendのみで構造変更しない
	ScriptComponent* component = world.TryGetComponent<ScriptComponent>(owner);
	if (!component) {
		component = &world.AddComponent<ScriptComponent>(owner);
	}

	// ScriptEntryをBufferへ追加してinstanceを即時生成する
	DynamicBuffer<ScriptEntry> entries =
		world.GetBuffer<ScriptEntry>(owner);
	ScriptEntry& entry =
		entries.EmplaceBack(MakeScriptEntry(info->scriptTypeID, info->name));
	const BehaviorHandle handle = activeSystem_->runtime_.Create(
		info->id, owner, entry.scriptSlotID);

	BehaviorRecord* record = activeSystem_->runtime_.GetRecord(handle);
	if (!record || !record->instance) {

		entries.RemoveAt(entries.GetSize() - 1);
		return false;
	}

	// scriptSlotIDを渡し、既定のserialized fieldsを適用してからinstanceを確定する
	record->instance->SetSlotID(entry.scriptSlotID.value);
	record->instance->SetSerializedFields(entry.serializedFields);
	if (!record->instance->EnsureInstance(world, owner)) {

		record->faulted = true;
	}
	world.MarkComponentModified<ScriptComponent>(owner);
	world.MarkComponentModified<ScriptEntry>(owner);
	activeSystem_->participantsDirty_ = true;
	activeSystem_->enableTransitionsDirty_ = true;
	return true;
}

void Engine::BehaviorSystem::EnsureActiveWorld(ECSWorld& world, SystemContext& context) {

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
		&BehaviorSystem::OnComponentMutation, this);
}

void Engine::BehaviorSystem::ResetRuntimeState(ECSWorld& world) {

	// participantキャッシュを破棄し、次のsynchronizeで作り直す
	participants_.clear();
	participantsDirty_ = true;
	dirtyScriptEntities_.clear();
	enableTransitionsDirty_ = true;
	fullSyncRequested_ = true;
	// 実行時対応はBehaviorWorldが所有し、Script設定側へキャッシュしない
	(void)world;
}

void Engine::BehaviorSystem::SynchronizeLifecycle(ECSWorld& world, SystemContext& context, bool sweep) {

	// ワールドを設定
	EnsureActiveWorld(world, context);
	// プレイモードでない、もしくはアクティブなワールドでないときは何もしない
	if (context.mode != WorldMode::Play || activeWorld_ != &world) {
		return;
	}

	// 初回とHot Reloadだけ全走査し、通常フレームは変更されたEntityだけ同期する
	if (fullSyncRequested_) {
		dirtyScriptEntities_.clear();
		SynchronizeRecords(world, context, sweep);
		fullSyncRequested_ = false;
	} else {
		SynchronizeDirtyRecords(world, context);
	}

	// 構造変更時だけparticipantキャッシュを作り直して安定ソートする
	if (participantsDirty_) {

		RebuildParticipants(world);
		participantsDirty_ = false;
	}

	// ScriptかActive状態が変わった場合だけライフサイクル遷移を再評価する
	const bool updateLifecycle = enableTransitionsDirty_;
	enableTransitionsDirty_ = false;
	if (updateLifecycle) {
		// Pass2 activeなものだけAwake
		InvokePendingAwake(world, context);
		// Pass3 OnEnable/OnDisable遷移
		ApplyEnableTransitions(world, context);
	}
	// Pass4 全Awake/OnEnable後Startより前にSceneLoaded/Unloadedを発火する
	ManagedScriptRuntime::GetInstance().PumpSceneEvents();

	if (updateLifecycle) {
		// Pass5 全件Start
		InvokePendingStart(world, context);
	}
}

void Engine::BehaviorSystem::SynchronizeRecords(ECSWorld& world, SystemContext& context, bool sweep) {

	// 全同期はWorld開始、Scene構成変更、Hot Reloadだけで実行する
	runtime_.ClearSeenFlags();
	world.ForEach<ScriptComponent>([&](Entity entity, ScriptComponent& component) {
		(void)component;
		SynchronizeEntityRecords(world, context, entity, false);
		});

	if (sweep) {
		if (runtime_.SweepUnseen(world, context) > 0) {
			participantsDirty_ = true;
		}
	}
	enableTransitionsDirty_ = true;
}

void Engine::BehaviorSystem::SynchronizeDirtyRecords(ECSWorld& world, SystemContext& context) {

	if (dirtyScriptEntities_.empty()) {
		return;
	}

	// 同期中のライフサイクルから発生した変更通知は次回分として残す
	std::vector<Entity> targets{};
	targets.swap(dirtyScriptEntities_);

	// 同一Entityへの複数変更通知を1回へまとめる
	std::sort(targets.begin(), targets.end(),
		[](const Entity& lhs, const Entity& rhs) {
			if (lhs.index != rhs.index) {
				return lhs.index < rhs.index;
			}
			return lhs.generation < rhs.generation;
		});
	targets.erase(std::unique(targets.begin(), targets.end()), targets.end());

	for (const Entity& entity : targets) {
		SynchronizeEntityRecords(world, context, entity, true);
	}
	enableTransitionsDirty_ = true;
}

void Engine::BehaviorSystem::SynchronizeEntityRecords(ECSWorld& world, SystemContext& context,
	const Entity& entity, bool clearOwnerSeen) {

	if (clearOwnerSeen) {
		runtime_.ClearSeenFlagsByOwner(entity);
	}

	if (!world.HasComponent<ScriptComponent>(entity)) {
		if (runtime_.DestroyByOwner(entity, world, context) > 0) {
			participantsDirty_ = true;
		}
		return;
	}

	const std::span<ScriptEntry> entries =
		GetScriptEntries(world, entity);
	for (size_t slot = 0; slot < entries.size(); ++slot) {

		ScriptEntry& entry = entries[slot];
		BehaviorHandle handle =
			runtime_.FindHandleBySlot(entity, entry.scriptSlotID);

		// GUIDも型名も空のスロットはビヘイビアを破棄する
		if (entry.scriptTypeID.empty() && entry.lastKnownTypeName.empty()) {
			if (handle.IsValid()) {
				runtime_.Destroy(handle, world, context);
				participantsDirty_ = true;
			}
			continue;
		}

		uint32_t typeID = 0;
		if (!TryResolveTypeID(entry, typeID)) {
			if (handle.IsValid()) {
				runtime_.Destroy(handle, world, context);
				participantsDirty_ = true;
			}
			continue;
		}

		BehaviorRecord* record = nullptr;
		if (runtime_.IsAlive(handle)) {
			record = runtime_.GetRecord(handle);
			if (!record || record->typeID != typeID || record->owner != entity) {
				runtime_.Destroy(handle, world, context);
				handle = BehaviorHandle::Null();
				record = nullptr;
				participantsDirty_ = true;
			}
		}
		if (!record) {
			handle = runtime_.Create(typeID, entity, entry.scriptSlotID);
			record = runtime_.GetRecord(handle);
			if (!record || !record->instance) {
				continue;
			}
			record->instance->SetSlotID(entry.scriptSlotID.value);
			participantsDirty_ = true;
		}

		record->seen = true;
		if (record->faulted || record->instance->IsFaulted()) {
			record->faulted = true;
			continue;
		}

		// この関数は初回または変更Entityだけを通るためJSON比較キャッシュは不要
		record->instance->SetSerializedFields(entry.serializedFields);

		if (!record->instance->EnsureInstance(world, entity)) {
			record->faulted = true;
		}
	}

	if (clearOwnerSeen && runtime_.SweepUnseenByOwner(entity, world, context) > 0) {
		participantsDirty_ = true;
	}
}

void Engine::BehaviorSystem::OnComponentMutation(ECSWorld& world, const Entity& entity,
	uint32_t typeID, ComponentMutationKind kind, void* userData) {

	auto* system = static_cast<BehaviorSystem*>(userData);
	if (!system || system->activeWorld_ != &world) {
		return;
	}

	if (kind == ComponentMutationKind::EntityDestroyed) {
		system->QueueScriptEntity(entity);
		system->enableTransitionsDirty_ = true;
		return;
	}

	ComponentTypeRegistry& registry = ComponentTypeRegistry::GetInstance();
	if (typeID == registry.GetID<ScriptComponent>() ||
		typeID == registry.GetID<ScriptEntry>()) {
		system->QueueScriptEntity(entity);
		system->participantsDirty_ = true;
		return;
	}
	if (typeID == registry.GetID<SceneObjectComponent>() ||
		typeID == registry.GetID<HierarchyComponent>()) {
		system->enableTransitionsDirty_ = true;
	}
}

void Engine::BehaviorSystem::QueueScriptEntity(const Entity& entity) {

	if (entity.IsValid()) {
		dirtyScriptEntities_.emplace_back(entity);
	}
	enableTransitionsDirty_ = true;
}

void Engine::BehaviorSystem::RebuildParticipants(ECSWorld& world) {

	// seenなrecordを集めて実行順で安定ソートする、構造変更時のみ呼ぶ
	participants_.clear();

	BehaviorTypeRegistry& typeRegistry = BehaviorTypeRegistry::GetInstance();

	world.ForEach<ScriptComponent>(
		[&](Entity entity, [[maybe_unused]] ScriptComponent& component) {

		const std::span<const ScriptEntry> entries =
			GetScriptEntries(world, entity);
		for (size_t slot = 0; slot < entries.size(); ++slot) {

			const ScriptEntry& entry = entries[slot];
			const BehaviorHandle handle =
				runtime_.FindHandleBySlot(entity, entry.scriptSlotID);
			if (!runtime_.IsAlive(handle)) {
				continue;
			}
			const BehaviorRecord* record = runtime_.GetRecord(handle);
			if (!record || !record->seen || !record->instance) {
				continue;
			}
			// 型へ設定された既定実行順を使う
			const int32_t executionOrder =
				typeRegistry.GetInfo(record->typeID).defaultExecutionOrder;
			participants_.emplace_back(SyncParticipant{
				handle, entity, static_cast<int32_t>(slot), executionOrder
				});
		}
			});

	std::sort(participants_.begin(), participants_.end(),
		[](const SyncParticipant& lhs, const SyncParticipant& rhs) {

			// 実行順が小さいものを先に、同値は既存の安定キーで決定的に解決する
			if (lhs.executionOrder != rhs.executionOrder) {
				return lhs.executionOrder < rhs.executionOrder;
			}
			if (lhs.owner.index != rhs.owner.index) {
				return lhs.owner.index < rhs.owner.index;
			}
			if (lhs.owner.generation != rhs.owner.generation) {
				return lhs.owner.generation < rhs.owner.generation;
			}
			return lhs.slot < rhs.slot;
		});
}

void Engine::BehaviorSystem::InvokePendingAwake(ECSWorld& world, SystemContext& context) {

	// activeかつ未AwakeのrecordにAwakeを1回呼ぶ
	for (const SyncParticipant& participant : participants_) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || record->faulted || record->awakeCalled) {
			continue;
		}
		// inactive hierarchyの間はAwakeを遅延する
		if (!IsEntityActiveInHierarchy(world, participant.owner)) {
			continue;
		}
		record->instance->Awake(world, context, participant.owner);
		record->awakeCalled = true;
		if (record->instance->IsFaulted()) {
			record->faulted = true;
		}
	}
}

bool Engine::BehaviorSystem::IsParticipantEnabled(ECSWorld& world,
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
	if (record.hasRuntimeEnabledOverride) {
		return record.runtimeEnabledOverride;
	}
	return entries[participant.slot].enabled;
}

void Engine::BehaviorSystem::ApplyEnableTransitions(ECSWorld& world, SystemContext& context) {

	for (const SyncParticipant& participant : participants_) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || record->faulted) {
			continue;
		}

		// OnEnableはAwake後かつactiveのときだけ呼ぶ
		const bool shouldBeEnabled =
			IsParticipantEnabled(world, participant, *record) && record->awakeCalled &&
			IsEntityActiveInHierarchy(world, participant.owner);

		if (shouldBeEnabled && !record->enabled) {

			record->instance->OnEnable(world, context, participant.owner);
			record->enabled = true;
		} else if (!shouldBeEnabled && record->enabled) {

			// 有効から無効への遷移時のみOnDisableを呼ぶ
			record->instance->OnDisable(world, context, participant.owner);
			record->enabled = false;
		}
		if (record->instance->IsFaulted()) {
			record->faulted = true;
		}
	}
}

void Engine::BehaviorSystem::InvokePendingStart(ECSWorld& world, SystemContext& context) {

	// 有効かつ未StartのrecordにStartを1回呼ぶ、再有効化では再実行しない
	for (const SyncParticipant& participant : participants_) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || record->faulted) {
			continue;
		}
		if (record->enabled && record->awakeCalled && !record->startCalled) {

			record->instance->Start(world, context, participant.owner);
			record->startCalled = true;
			if (record->instance->IsFaulted()) {
				record->faulted = true;
			}
		}
	}
}

void Engine::BehaviorSystem::DispatchCollision(ECSWorld& world,
	SystemContext& context, const CollisionContact& collision, int32_t phase) {

	if (context.mode != WorldMode::Play || activeWorld_ != &world) {
		return;
	}

	// Contactのselfに一致するEntityのビヘイビアだけへ通知する
	runtime_.ForEachAliveByOwner(collision.self, [&](BehaviorRecord& record) {

		if (!record.enabled || !record.instance || record.faulted) {
			return;
		}
		switch (phase) {
		case 0:
			record.instance->OnCollisionEnter(world, context, collision);
			break;
		case 1:
			record.instance->OnCollisionStay(world, context, collision);
			break;
		case 2:
			record.instance->OnCollisionExit(world, context, collision);
			break;
		default:
			break;
		}
		if (record.instance->IsFaulted()) {
			record.faulted = true;
		}
		});
}
