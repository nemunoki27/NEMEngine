#include "BehaviorSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ScriptExecutionOrderTable.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedScriptProfilerStore.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <chrono>

//============================================================================
//	BehaviorSystem classMethods
//============================================================================

Engine::BehaviorSystem* Engine::BehaviorSystem::activeSystem_ = nullptr;

namespace {

	// ScriptEntryをGUID優先でランタイム型IDへ解決する
	bool TryResolveTypeID(Engine::ScriptEntry& entry, uint32_t& outTypeID) {

		// 解決済みならキャッシュをそのまま返す
		if (entry.resolvedRuntimeTypeValid) {
			outTypeID = entry.resolvedRuntimeTypeID;
			return true;
		}

		auto& registry = Engine::BehaviorTypeRegistry::GetInstance();

		// まずGUIDで解決する
		if (!entry.scriptTypeId.empty()) {

			if (const Engine::BehaviorTypeInfo* info = registry.FindByStableScriptTypeID(entry.scriptTypeId)) {

				entry.lastKnownTypeName = info->name;
				entry.resolvedRuntimeTypeID = info->id;
				entry.resolvedRuntimeTypeValid = true;
				outTypeID = info->id;
				return true;
			}
			// GUIDが現manifestに無いときはフォールバックしない
			return false;
		}

		// 旧データは型名から解決してGUIDを補完する
		if (entry.lastKnownTypeName.empty()) {
			return false;
		}
		const Engine::BehaviorTypeInfo* info = registry.FindByName(entry.lastKnownTypeName);
		if (!info) {
			// 完全名で無ければ単純名で解決し、曖昧ならnullptr
			const std::string simpleName = Engine::MakeSimpleTypeName(entry.lastKnownTypeName);
			info = registry.FindManagedBySimpleName(simpleName);
		}
		if (!info) {
			// 解決不能ならMissing Scriptとしてデータは保持する
			return false;
		}

		// 解決できたGUIDを主キーへ書き込む
		entry.scriptTypeId = info->scriptTypeId;
		entry.lastKnownTypeName = info->name;
		entry.resolvedRuntimeTypeID = info->id;
		entry.resolvedRuntimeTypeValid = true;
		outTypeID = info->id;
		Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::info,
			"BehaviorSystem: migrated legacy script '{}' to scriptTypeId={}", info->name, info->scriptTypeId);
		return true;
	}
}

void Engine::BehaviorSystem::OnWorldEnter(ECSWorld& world, SystemContext& context) {

	activeSystem_ = this;
	// ワールドをアクティブにする
	EnsureActiveWorld(world, context);

	// プレイモードでワールドに入ったときは、スクリプトのビヘイビアの実体化と初期化を行う
	if (context.mode == WorldMode::Play) {

		// 新しいPlayの計測のためdetail profilerをresetする
		ManagedScriptProfilerStore::GetInstance().Reset();
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
	activeWorld_ = nullptr;
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
		// type slot単位で所要時間を計測する、Releaseでは無効
		ScriptProfileSample sample(record->typeID, record->owner.index, participant.slot, ScriptCallbackKind::FixedUpdate);
		record->instance->FixedUpdate(world, context, record->owner);
		if (record->instance->IsFaulted()) {
			record->faulted = true;
			sample.MarkFaulted();
		}
	}

	// FixedUpdate末でWaitForFixedUpdateのコルーチンをresumeする
	if constexpr (ManagedScriptProfilerStore::kDetailEnabled) {
		const auto t0 = std::chrono::high_resolution_clock::now();
		ManagedScriptRuntime::GetInstance().TickFrame(1, context);
		const std::chrono::duration<float, std::milli> ms = std::chrono::high_resolution_clock::now() - t0;
		ManagedScriptProfilerStore::GetInstance().RecordCoroutineResume(ms.count());
	}
	else {
		ManagedScriptRuntime::GetInstance().TickFrame(1, context);
	}
}

void Engine::BehaviorSystem::Update(ECSWorld& world, SystemContext& context) {

	// プレイモード以外は処理しない
	if (context.mode != WorldMode::Play) {
		return;
	}

	// Update前にライフサイクルを同期し、不要なビヘイビアをsweepする
	SynchronizeLifecycle(world, context, true);

	for (const SyncParticipant& participant : participants_) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || !record->enabled || record->faulted) {
			continue;
		}
		ScriptProfileSample sample(record->typeID, record->owner.index, participant.slot, ScriptCallbackKind::Update);
		record->instance->Update(world, context, record->owner);
		if (record->instance->IsFaulted()) {
			record->faulted = true;
			sample.MarkFaulted();
		}
	}

	// Update末でTimerとコルーチンを駆動する
	if constexpr (ManagedScriptProfilerStore::kDetailEnabled) {
		const auto t0 = std::chrono::high_resolution_clock::now();
		ManagedScriptRuntime::GetInstance().TickFrame(0, context);
		const std::chrono::duration<float, std::milli> ms = std::chrono::high_resolution_clock::now() - t0;
		ManagedScriptProfilerStore::GetInstance().RecordCoroutineResume(ms.count());
	}
	else {
		ManagedScriptRuntime::GetInstance().TickFrame(0, context);
	}
}

void Engine::BehaviorSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// プレイモード以外、もしくはアクティブワールドでないときは処理しない
	if (context.mode != WorldMode::Play || activeWorld_ != &world) {
		return;
	}

	// LateUpdateはUpdateで確定したparticipantのみ実行する
	for (const SyncParticipant& participant : participants_) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || !record->enabled || record->faulted) {
			continue;
		}
		ScriptProfileSample sample(record->typeID, record->owner.index, participant.slot, ScriptCallbackKind::LateUpdate);
		record->instance->LateUpdate(world, context, record->owner);
		if (record->instance->IsFaulted()) {
			record->faulted = true;
			sample.MarkFaulted();
		}
	}

	// LateUpdate末でWaitForEndOfFrameのコルーチンをresumeする
	if constexpr (ManagedScriptProfilerStore::kDetailEnabled) {
		const auto t0 = std::chrono::high_resolution_clock::now();
		ManagedScriptRuntime::GetInstance().TickFrame(2, context);
		const std::chrono::duration<float, std::milli> ms = std::chrono::high_resolution_clock::now() - t0;
		ManagedScriptProfilerStore::GetInstance().RecordCoroutineResume(ms.count());
	}
	else {
		ManagedScriptRuntime::GetInstance().TickFrame(2, context);
	}
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
	const std::string& fieldId, const nlohmann::json& value) {

	if (!activeSystem_ || !activeSystem_->runtime_.IsAlive(handle)) {
		return;
	}
	BehaviorRecord* record = activeSystem_->runtime_.GetRecord(handle);
	if (record && record->instance) {
		record->instance->SetRuntimeSerializedField(fieldId, value);
	}
}

namespace {

	// active worldのowner Entity上でscriptSlotID一致のScriptEntryを探す
	Engine::ScriptEntry* FindScriptEntryBySlot(Engine::ECSWorld& world, const Engine::Entity& owner, const Engine::UUID& slotId) {

		Engine::ScriptComponent* component = world.TryGetComponent<Engine::ScriptComponent>(owner);
		if (!component) {
			return nullptr;
		}
		for (Engine::ScriptEntry& entry : component->scripts) {
			if (entry.scriptSlotID == slotId) {
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
	if (BehaviorRecord* record = activeSystem_->runtime_.GetRecord(entry->handle)) {
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
	if (BehaviorRecord* record = activeSystem_->runtime_.GetRecord(entry->handle)) {
		record->runtimeEnabledOverride = enabled;
		record->hasRuntimeEnabledOverride = true;
	}
}

Engine::MonoBehavior* Engine::BehaviorSystem::FindScriptInstance(const Entity& owner, const std::string& scriptTypeId) {

	if (!activeSystem_ || !activeSystem_->activeWorld_) {
		return nullptr;
	}

	// owner上でscriptTypeId一致のScriptEntryを探しinstanceを返す
	ScriptComponent* component = activeSystem_->activeWorld_->TryGetComponent<ScriptComponent>(owner);
	if (!component) {
		return nullptr;
	}
	for (ScriptEntry& entry : component->scripts) {
		if (entry.scriptTypeId != scriptTypeId) {
			continue;
		}
		if (BehaviorRecord* record = activeSystem_->runtime_.GetRecord(entry.handle)) {
			if (record->instance) {
				return record->instance.get();
			}
		}
	}
	return nullptr;
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

		runtime_.DestroyAll(*activeWorld_, context);
	}

	// 新しいワールドをアクティブにする
	activeWorld_ = &world;
	ResetRuntimeState(world);
}

void Engine::BehaviorSystem::ResetRuntimeState(ECSWorld& world) {

	// participantキャッシュを破棄し、次のsynchronizeで作り直す
	participants_.clear();
	participantsDirty_ = true;

	// 全Scriptのruntimeキャッシュを無効化する、次のsyncで作り直す
	world.ForEach<ScriptComponent>([&](Entity, ScriptComponent& component) {

		for (auto& entry : component.scripts) {

			// 永続データは消さずruntimeキャッシュだけ無効化する
			entry.handle = BehaviorHandle::Null();
			entry.resolvedRuntimeTypeID = 0;
			entry.resolvedRuntimeTypeValid = false;
		}
		});
}

void Engine::BehaviorSystem::SynchronizeLifecycle(ECSWorld& world, SystemContext& context, bool sweep) {

	// ワールドを設定
	EnsureActiveWorld(world, context);
	// プレイモードでない、もしくはアクティブなワールドでないときは何もしない
	if (context.mode != WorldMode::Play || activeWorld_ != &world) {
		return;
	}

	// Pass1 record同期と型解決とinstance生成、gameplay callbackは呼ばない
	SynchronizeRecords(world, context, sweep);

	// 構造変更時だけparticipantキャッシュを作り直して安定ソートする
	if (participantsDirty_) {

		RebuildParticipants(world);
		participantsDirty_ = false;
	}

	// Pass2 activeなものだけAwake
	InvokePendingAwake(world, context);
	// Pass3 OnEnable/OnDisable遷移
	ApplyEnableTransitions(world, context);
	// Pass4 全Awake/OnEnable後Startより前にSceneLoaded/Unloadedを発火する
	ManagedScriptRuntime::GetInstance().PumpSceneEvents();

	// Pass5 全件Start
	InvokePendingStart(world, context);
}

void Engine::BehaviorSystem::SynchronizeRecords(ECSWorld& world, SystemContext& context, bool sweep) {

	// フラグリセット
	runtime_.ClearSeenFlags();

	// 全Scriptを走査しrecordを同期する、ここではgameplay callbackを呼ばない
	world.ForEach<ScriptComponent>([&](Entity entity, ScriptComponent& component) {

		for (size_t slot = 0; slot < component.scripts.size(); ++slot) {

			ScriptEntry& entry = component.scripts[slot];

			// GUIDも型名も空のスロットはビヘイビアを破棄する
			if (entry.scriptTypeId.empty() && entry.lastKnownTypeName.empty()) {
				if (entry.handle.IsValid()) {

					runtime_.Destroy(entry.handle, world, context);
					entry.handle = BehaviorHandle::Null();
					participantsDirty_ = true;
				}
				continue;
			}

			uint32_t typeID = 0;
			if (!TryResolveTypeID(entry, typeID)) {
				if (entry.handle.IsValid()) {

					runtime_.Destroy(entry.handle, world, context);
					entry.handle = BehaviorHandle::Null();
					participantsDirty_ = true;
				}
				continue;
			}

			// ハンドルが有効で、かつ生存しているならレコードを取得する
			BehaviorRecord* record = nullptr;
			if (runtime_.IsAlive(entry.handle)) {

				record = runtime_.GetRecord(entry.handle);
				// 無効の場合はハンドルを破棄して無効にする
				if (!record || record->typeID != typeID || record->owner != entity) {

					runtime_.Destroy(entry.handle, world, context);
					entry.handle = BehaviorHandle::Null();
					record = nullptr;
					participantsDirty_ = true;
				}
			}
			// ハンドルが無効なら新しくビヘイビアを生成する
			if (!record) {

				entry.handle = runtime_.Create(typeID, entity);
				record = runtime_.GetRecord(entry.handle);
				// 生成に失敗している場合はハンドルを破棄して無効にする
				if (!record || !record->instance) {
					entry.handle = BehaviorHandle::Null();
					continue;
				}
				// scriptSlotIDをinstanceへ渡す、C#側が自身のentryを特定するのに使う
				record->instance->SetSlotId(entry.scriptSlotID.value);
				participantsDirty_ = true;
			}

			// アクセスされたフラグを立てる
			record->seen = true;

			// faulted状態のビヘイビアは生存させたまま以降の初期化を行わない
			if (record->faulted || record->instance->IsFaulted()) {

				record->faulted = true;
				continue;
			}

			// serializedFieldsはrevisionが進んだときだけ適用しhot pathでJSONを触らない
			if (record->appliedSerializedRevision != entry.serializedRevision) {

				record->instance->SetSerializedFields(entry.serializedFields);
				record->appliedSerializedRevision = entry.serializedRevision;
			}

			// inactiveでもinstanceは生成し、生成失敗はfaultedにして除外する
			if (!record->instance->EnsureInstance(world, entity)) {

				record->faulted = true;
			}
		}
		});

	// 更新時、参照されなくなったビヘイビアをワールドから破棄する
	if (sweep) {

		if (runtime_.SweepUnseen(world, context) > 0) {
			participantsDirty_ = true;
		}
	}
}

void Engine::BehaviorSystem::InvalidateExecutionOrder() {

	// 設定ファイルを読み直し、次のSynchronizeLifecycleでparticipantを再ソートさせる
	ScriptExecutionOrderTable::GetInstance().Reload();
	if (activeSystem_) {
		activeSystem_->participantsDirty_ = true;
	}
}

void Engine::BehaviorSystem::RebuildParticipants(ECSWorld& world) {

	// seenなrecordを集めて実行順で安定ソートする、構造変更時のみ呼ぶ
	participants_.clear();

	// GUID単位の実行順overrideを解決する、未設定は0
	ScriptExecutionOrderTable& orderTable = ScriptExecutionOrderTable::GetInstance();
	orderTable.EnsureLoaded();
	BehaviorTypeRegistry& typeRegistry = BehaviorTypeRegistry::GetInstance();

	world.ForEach<ScriptComponent>([&](Entity entity, ScriptComponent& component) {

		for (size_t slot = 0; slot < component.scripts.size(); ++slot) {

			const ScriptEntry& entry = component.scripts[slot];
			if (!runtime_.IsAlive(entry.handle)) {
				continue;
			}
			const BehaviorRecord* record = runtime_.GetRecord(entry.handle);
			if (!record || !record->seen || !record->instance) {
				continue;
			}
			// 実行順はoverride優先、未設定はDefaultExecutionOrder
			const BehaviorTypeInfo& typeInfo = typeRegistry.GetInfo(record->typeID);
			int32_t executionOrder = typeInfo.defaultExecutionOrder;
			int32_t overrideValue = 0;
			if (!typeInfo.scriptTypeId.empty() && orderTable.TryGetOverride(typeInfo.scriptTypeId, overrideValue)) {
				executionOrder = overrideValue;
			}
			participants_.emplace_back(SyncParticipant{ entry.handle, entity, static_cast<int32_t>(slot), executionOrder });
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

void Engine::BehaviorSystem::ApplyEnableTransitions(ECSWorld& world, SystemContext& context) {

	for (const SyncParticipant& participant : participants_) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || record->faulted) {
			continue;
		}

		// enabledは都度評価する、runtime overrideがあれば優先しauthoringへ書き戻さない
		bool entryEnabled = true;
		if (record->hasRuntimeEnabledOverride) {
			entryEnabled = record->runtimeEnabledOverride;
		} else if (ScriptComponent* component = world.TryGetComponent<ScriptComponent>(participant.owner)) {
			if (0 <= participant.slot && static_cast<size_t>(participant.slot) < component->scripts.size()) {
				entryEnabled = component->scripts[participant.slot].enabled;
			}
		}

		// OnEnableはAwake後かつactiveのときだけ呼ぶ
		const bool shouldBeEnabled =
			entryEnabled && record->awakeCalled && IsEntityActiveInHierarchy(world, participant.owner);

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
