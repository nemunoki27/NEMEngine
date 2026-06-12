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

	// ScriptEntryをStable Script Type GUID優先でcompact runtime type IDへ解決する、解決済みはcache resolvedRuntimeTypeValidを返しhot pathで文字列検索しない、解決不能はMissing Scriptとしてfalseでデータは保持する
	bool TryResolveTypeID(Engine::ScriptEntry& entry, uint32_t& outTypeID) {

		// reload境界ResetRuntimeStateでcacheは無効化される、resolved済みは即返す
		if (entry.resolvedRuntimeTypeValid) {
			outTypeID = entry.resolvedRuntimeTypeID;
			return true;
		}

		auto& registry = Engine::BehaviorTypeRegistry::GetInstance();

		// 1. Stable GUID永続主キーで解決する
		if (!entry.scriptTypeId.empty()) {

			if (const Engine::BehaviorTypeInfo* info = registry.FindByStableScriptTypeID(entry.scriptTypeId)) {

				entry.lastKnownTypeName = info->name;
				entry.resolvedRuntimeTypeID = info->id;
				entry.resolvedRuntimeTypeValid = true;
				outTypeID = info->id;
				return true;
			}
			// GUIDはあるが現manifestに無いMissing Scriptでlegacyへフォールバックしない
			return false;
		}

		// 2. legacy移行: lastKnownTypeNameから一意に解決できればGUIDを書き込む
		if (entry.lastKnownTypeName.empty()) {
			return false;
		}
		const Engine::BehaviorTypeInfo* info = registry.FindByName(entry.lastKnownTypeName);
		if (!info) {
			// 完全名で無ければ単純名で解決し複数候補なら曖昧としてnullptrで自動移行しない
			const std::string simpleName = Engine::MakeSimpleTypeName(entry.lastKnownTypeName);
			info = registry.FindManagedBySimpleName(simpleName);
		}
		if (!info) {
			// 解決不能な不明or曖昧はMissing Scriptでデータは保持する
			return false;
		}

		// 移行成功: GUIDを主キーへ書き込む、次回保存でGUID形式になる
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

		// 新しいPlay sessionの計測を0から取るためdetail profilerをresetする、aggregateは維持
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

	// fixed substepごとにライフサイクルを同期してから固定更新を回す
	SynchronizeLifecycle(world, context, false);

	// 確定済みのparticipantスナップショットを決定的な順序で実行する
	for (const SyncParticipant& participant : participants_) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || !record->enabled || record->faulted) {
			continue;
		}
		// detail profiler: type slot FixedUpdate単位で所要時間を計測する、Releaseでは無効化
		ScriptProfileSample sample(record->typeID, record->owner.index, participant.slot, ScriptCallbackKind::FixedUpdate);
		record->instance->FixedUpdate(world, context, record->owner);
		if (record->instance->IsFaulted()) {
			record->faulted = true;
			sample.MarkFaulted();
		}
	}

	// FixedUpdate phase末: WaitForFixedUpdateのcoroutineをresumeする、detail有効時は所要時間も計測
	if constexpr (ManagedScriptProfilerStore::kDetailEnabled) {
		const auto t0 = std::chrono::high_resolution_clock::now();
		ManagedScriptRuntime::GetInstance().TickFrame(1);
		const std::chrono::duration<float, std::milli> ms = std::chrono::high_resolution_clock::now() - t0;
		ManagedScriptProfilerStore::GetInstance().RecordCoroutineResume(ms.count());
	}
	else {
		ManagedScriptRuntime::GetInstance().TickFrame(1);
	}
}

void Engine::BehaviorSystem::Update(ECSWorld& world, SystemContext& context) {

	// プレイモード以外は処理しない
	if (context.mode != WorldMode::Play) {
		return;
	}

	// Update前にライフサイクルを同期し、不要になったビヘイビアをsweepする
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

	// Update phase末: Timer tickとCoroutine Updateを駆動する、detail有効時は所要時間も計測
	if constexpr (ManagedScriptProfilerStore::kDetailEnabled) {
		const auto t0 = std::chrono::high_resolution_clock::now();
		ManagedScriptRuntime::GetInstance().TickFrame(0);
		const std::chrono::duration<float, std::milli> ms = std::chrono::high_resolution_clock::now() - t0;
		ManagedScriptProfilerStore::GetInstance().RecordCoroutineResume(ms.count());
	}
	else {
		ManagedScriptRuntime::GetInstance().TickFrame(0);
	}
}

void Engine::BehaviorSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// プレイモード以外、もしくはアクティブワールドでないときは処理しない
	if (context.mode != WorldMode::Play || activeWorld_ != &world) {
		return;
	}

	// LateUpdateではsynchronizeせずUpdateで確定したparticipantスナップショットのみを実行する、Update後flushで生成されたEntity/scriptは同フレームのLateUpdateへ途中参加しない
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

	// LateUpdate phase末: WaitForEndOfFrameのcoroutineをresumeする、detail有効時は所要時間も計測
	if constexpr (ManagedScriptProfilerStore::kDetailEnabled) {
		const auto t0 = std::chrono::high_resolution_clock::now();
		ManagedScriptRuntime::GetInstance().TickFrame(2);
		const std::chrono::duration<float, std::milli> ms = std::chrono::high_resolution_clock::now() - t0;
		ManagedScriptProfilerStore::GetInstance().RecordCoroutineResume(ms.count());
	}
	else {
		ManagedScriptRuntime::GetInstance().TickFrame(2);
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

	// Play中のlive instanceの現在値を返す、非アクティブや未生存handleは空
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
	// runtime overrideがあればそれ、無ければauthoring enabledを返す
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
	// runtime overrideを立て次のlifecycle sync境界ApplyEnableTransitionsでOnEnable/OnDisableが反映される、authoringのScriptEntry.enabledは変更せずPlay終了でrecordごと破棄される
	if (BehaviorRecord* record = activeSystem_->runtime_.GetRecord(entry->handle)) {
		record->runtimeEnabledOverride = enabled;
		record->hasRuntimeEnabledOverride = true;
	}
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

	// スクリプトコンポーネントを持つ全てのエンティティに対して、スクリプトのビヘイビアの実体化と初期化を行う
	world.ForEach<ScriptComponent>([&](Entity, ScriptComponent& component) {
		for (auto& entry : component.scripts) {

			// scriptTypeIdとlastKnownTypeNameの永続データは消さずruntimeキャッシュだけ無効化する
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

	// Pass1: record同期・型解決・instance生成・serialized適用でgameplay callbackは呼ばない
	SynchronizeRecords(world, context, sweep);

	// 構造変更があったときだけparticipantキャッシュを作り直して安定ソートする
	if (participantsDirty_) {

		RebuildParticipants(world);
		participantsDirty_ = false;
	}

	// Pass2: activeなものだけ全件Awake
	InvokePendingAwake(world, context);
	// Pass3:全件OnEnable/OnDisable遷移
	ApplyEnableTransitions(world, context);
	//============================================================================
	//	Pass4: SceneLoaded/SceneUnloaded通知で全Awake/OnEnable完了後Startより前
	//	nativeのscene instance生存変化をC#側がpollしSceneLoadedとSceneUnloadedを発火する
	//============================================================================
	ManagedScriptRuntime::GetInstance().PumpSceneEvents();

	// Pass5:全件Start
	InvokePendingStart(world, context);
}

void Engine::BehaviorSystem::SynchronizeRecords(ECSWorld& world, SystemContext& context, bool sweep) {

	// フラグリセット
	runtime_.ClearSeenFlags();

	// スクリプトコンポーネントを持つ全エンティティを走査してrecordを同期する、gameplay callbackはここで呼ばないため走査中にECS chunkは壊れない
	world.ForEach<ScriptComponent>([&](Entity entity, ScriptComponent& component) {
		for (size_t slot = 0; slot < component.scripts.size(); ++slot) {

			ScriptEntry& entry = component.scripts[slot];

			// 型の手掛かりが何も無いGUIDも型名も空の空スロットはビヘイビアを破棄して無効にする
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
				// scriptSlotIDをinstanceへ渡す、C#側ScriptBehaviour.Enabledがowner EntityとscriptSlotIDで自身のruntime entryを特定するために使う
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

			// serializedFieldsはrevisionが進んだときだけ適用しhot pathで毎回JSONを触らない、新規record時はsentinelと一致しないので生成前に一度だけ適用される
			if (record->appliedSerializedRevision != entry.serializedRevision) {

				record->instance->SetSerializedFields(entry.serializedFields);
				record->appliedSerializedRevision = entry.serializedRevision;
			}

			// inactive hierarchyでもinstanceは全件生成しておきAwakeは後段のPass2でactive時のみ呼ぶ、生成に失敗したものはfaultedにして以後除外しretryやJSON stormを防ぐ
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

	// seenなalive recordを集めてexecutionOrder owner.index owner.generation slotで安定ソートする、構造変更があったフレームだけ呼ばれるため通常フレームではソートを行わない
	participants_.clear();

	// Script Type GUID単位の実行順を解決するproject-levelのoverrideで未設定は0、構造変更時のみここで参照するためgameplay hot pathには乗らない
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
			// 実行順のprecedenceはEditor project override > [DefaultExecutionOrder] > 0で、override無しと明示0を区別するためTryGetOverrideを使う
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

	// active hierarchyに入っていて未AwakeのrecordだけにAwakeを1回呼び、全participantのAwakeを完了させてから後段のStartへ進む
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

		// entry.enabledは構造変更なしでも変わり得るため都度評価しECSアクセスはO(1)、ScriptBehaviour.Enabledが立てたruntime overrideがあればそれを優先しauthoringへは書き戻さない
		bool entryEnabled = true;
		if (record->hasRuntimeEnabledOverride) {
			entryEnabled = record->runtimeEnabledOverride;
		} else if (ScriptComponent* component = world.TryGetComponent<ScriptComponent>(participant.owner)) {
			if (0 <= participant.slot && static_cast<size_t>(participant.slot) < component->scripts.size()) {
				entryEnabled = component->scripts[participant.slot].enabled;
			}
		}

		// OnEnableはAwake後にのみ呼びactiveInHierarchyも都度評価するO(1)のcached読み
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

	// 全Awake完了後に有効かつ未StartのrecordへStartを1回呼ぶ、再有効化ではstartCalledが残るため再実行しない
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
