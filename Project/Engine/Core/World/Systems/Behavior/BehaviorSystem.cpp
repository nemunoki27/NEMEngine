#include "BehaviorSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>

//============================================================================
//	BehaviorSystem classMethods
//============================================================================
Engine::BehaviorSystem* Engine::BehaviorSystem::activeSystem_ = nullptr;

namespace {

	// ScriptEntry を Stable Script Type GUID 優先で compact runtime type ID へ解決する。
	// 解決済みは cache（resolvedRuntimeTypeValid）を返す＝hot pathで文字列検索しない。
	// 解決不能は Missing Script として false（データは保持する）。
	bool TryResolveTypeID(Engine::ScriptEntry& entry, uint32_t& outTypeID) {

		// reload境界（ResetRuntimeState）で cache は無効化される。resolved済みは即返す
		if (entry.resolvedRuntimeTypeValid) {
			outTypeID = entry.resolvedRuntimeTypeID;
			return true;
		}

		auto& registry = Engine::BehaviorTypeRegistry::GetInstance();

		// 1. Stable GUID（永続主キー）で解決する
		if (!entry.scriptTypeId.empty()) {

			if (const Engine::BehaviorTypeInfo* info = registry.FindByStableScriptTypeID(entry.scriptTypeId)) {

				entry.lastKnownTypeName = info->name;
				entry.resolvedRuntimeTypeID = info->id;
				entry.resolvedRuntimeTypeValid = true;
				outTypeID = info->id;
				return true;
			}
			// GUIDはあるが現manifestに無い＝Missing Script。legacyへフォールバックしない
			return false;
		}

		// 2. legacy 移行: lastKnownTypeName から一意に解決できれば GUID を書き込む
		if (entry.lastKnownTypeName.empty()) {
			return false;
		}
		const Engine::BehaviorTypeInfo* info = registry.FindByName(entry.lastKnownTypeName);
		if (!info) {
			// 完全名で無ければ単純名（複数候補なら曖昧＝nullptrで自動移行しない）
			const std::string simpleName = Engine::MakeSimpleTypeName(entry.lastKnownTypeName);
			info = registry.FindManagedBySimpleName(simpleName);
		}
		if (!info) {
			// 解決不能（不明 or 曖昧）＝Missing Script。データは保持する
			return false;
		}

		// 移行成功: GUID を主キーへ書き込む（次回保存でGUID形式になる）
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
		record->instance->FixedUpdate(world, context, record->owner);
		if (record->instance->IsFaulted()) {
			record->faulted = true;
		}
	}

	// FixedUpdate phase 末: WaitForFixedUpdate の coroutine を resume する
	ManagedScriptRuntime::GetInstance().TickFrame(1);
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
		record->instance->Update(world, context, record->owner);
		if (record->instance->IsFaulted()) {
			record->faulted = true;
		}
	}

	// Update phase 末: Timer tick と Coroutine(Update) を駆動する
	ManagedScriptRuntime::GetInstance().TickFrame(0);
}

void Engine::BehaviorSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// プレイモード以外、もしくはアクティブワールドでないときは処理しない
	if (context.mode != WorldMode::Play || activeWorld_ != &world) {
		return;
	}

	// LateUpdateではsynchronizeしない。Updateで確定したparticipantスナップショットのみを実行する。
	// Update後flushで生成されたEntity/scriptは同フレームのLateUpdateへ途中参加しない。
	for (const SyncParticipant& participant : participants_) {

		BehaviorRecord* record = runtime_.GetRecord(participant.handle);
		if (!record || !record->instance || !record->enabled || record->faulted) {
			continue;
		}
		record->instance->LateUpdate(world, context, record->owner);
		if (record->instance->IsFaulted()) {
			record->faulted = true;
		}
	}

	// LateUpdate phase 末: WaitForEndOfFrame の coroutine を resume する
	ManagedScriptRuntime::GetInstance().TickFrame(2);
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

	// Play中の live instance の現在値を返す。非アクティブ/未生存handleは空
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

	// active world の owner Entity 上で scriptSlotID 一致の ScriptEntry を探す
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
	// runtime override があればそれ、無ければ authoring enabled を返す
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
	// runtime override を立てる。次の lifecycle sync 境界(ApplyEnableTransitions)で OnEnable/OnDisable が反映される。
	// authoring の ScriptEntry.enabled は変更しない（Play終了でrecordごと破棄される）
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

			// scriptTypeId / lastKnownTypeName（永続データ）は消さず、runtimeキャッシュだけ無効化する
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

	// Pass1: record同期・型解決・instance生成・serialized適用（gameplay callbackは呼ばない）
	SynchronizeRecords(world, context, sweep);

	// 構造変更があったときだけparticipantキャッシュを作り直して安定ソートする
	if (participantsDirty_) {

		RebuildParticipants(world);
		participantsDirty_ = false;
	}

	// Pass2: 全件Awake（activeなものだけ）
	InvokePendingAwake(world, context);
	// Pass3: 全件OnEnable/OnDisable遷移
	ApplyEnableTransitions(world, context);
	//============================================================================
	//	Pass4: SceneLoaded/SceneUnloaded通知（全Awake/OnEnable完了後・Startより前）
	//	07で C# SceneManager を導入。native の scene instance 生存変化を C# 側がpollして
	//	SceneLoaded（load完了）/ SceneUnloaded（unload完了）を発火する。
	//============================================================================
	ManagedScriptRuntime::GetInstance().PumpSceneEvents();

	// Pass5: 全件Start
	InvokePendingStart(world, context);
}

void Engine::BehaviorSystem::SynchronizeRecords(ECSWorld& world, SystemContext& context, bool sweep) {

	// フラグリセット
	runtime_.ClearSeenFlags();

	// スクリプトコンポーネントを持つ全てのエンティティを走査してrecordを同期する。
	// gameplay callbackはここでは呼ばないため、走査中にECS chunkは壊れない。
	world.ForEach<ScriptComponent>([&](Entity entity, ScriptComponent& component) {
		for (size_t slot = 0; slot < component.scripts.size(); ++slot) {

			ScriptEntry& entry = component.scripts[slot];

			// 型の手掛かりが何も無い（GUIDも型名も空）空スロットはビヘイビアを破棄して無効にする
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
				// scriptSlotID を instance へ渡す。C# 側 ScriptBehaviour.Enabled が
				// owner Entity + scriptSlotID で自身の runtime entry を特定するために使う
				record->instance->SetSlotId(entry.scriptSlotID.value);
				participantsDirty_ = true;
			}

			// アクセスされたフラグを立てる
			record->seen = true;

			// faulted状態のビヘイビアは生存させたまま、以降の初期化を行わない
			if (record->faulted || record->instance->IsFaulted()) {

				record->faulted = true;
				continue;
			}

			// serializedFieldsはrevisionが進んだときだけ適用する（hot pathで毎回JSONを触らない）。
			// 新規record時はsentinelと一致しないので、生成前に一度だけ適用される。
			if (record->appliedSerializedRevision != entry.serializedRevision) {

				record->instance->SetSerializedFields(entry.serializedFields);
				record->appliedSerializedRevision = entry.serializedRevision;
			}

			// inactive hierarchyでもinstanceは全件生成しておく（Awakeは後段のPass2でactive時のみ）。
			// 生成に失敗したものはfaultedにして以後除外する（retryやJSON storm防止）。
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

void Engine::BehaviorSystem::RebuildParticipants(ECSWorld& world) {

	// seenなalive recordを集めて、(owner.index, owner.generation, slot)で安定ソートする。
	// 構造変更があったフレームだけ呼ばれるため、通常フレームではソートを行わない。
	participants_.clear();
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
			participants_.emplace_back(SyncParticipant{ entry.handle, entity, static_cast<int32_t>(slot) });
		}
		});

	std::sort(participants_.begin(), participants_.end(),
		[](const SyncParticipant& lhs, const SyncParticipant& rhs) {

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

	// active hierarchyに入っていて未AwakeのrecordだけにAwakeを1回呼ぶ。
	// 全participantのAwakeをここで完了させてから後段のStartへ進む。
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

		// entry.enabledは構造変更なしでも変わり得るため都度評価する（ECSアクセスはO(1)）。
		// ScriptBehaviour.Enabled が立てた runtime override があればそれを優先する（authoringへは書き戻さない）
		bool entryEnabled = true;
		if (record->hasRuntimeEnabledOverride) {
			entryEnabled = record->runtimeEnabledOverride;
		} else if (ScriptComponent* component = world.TryGetComponent<ScriptComponent>(participant.owner)) {
			if (0 <= participant.slot && static_cast<size_t>(participant.slot) < component->scripts.size()) {
				entryEnabled = component->scripts[participant.slot].enabled;
			}
		}

		// OnEnableはAwake後にのみ呼ぶ。activeInHierarchyも都度評価する（O(1)のcached読み）
		const bool shouldBeEnabled =
			entryEnabled && record->awakeCalled && IsEntityActiveInHierarchy(world, participant.owner);

		if (shouldBeEnabled && !record->enabled) {

			record->instance->OnEnable(world, context, participant.owner);
			record->enabled = true;
		} else if (!shouldBeEnabled && record->enabled) {

			// 有効→無効への遷移時のみOnDisableを呼ぶ
			record->instance->OnDisable(world, context, participant.owner);
			record->enabled = false;
		}
		if (record->instance->IsFaulted()) {
			record->faulted = true;
		}
	}
}

void Engine::BehaviorSystem::InvokePendingStart(ECSWorld& world, SystemContext& context) {

	// 全Awake完了後に、有効かつ未StartのrecordへStartを1回呼ぶ。
	// 再有効化ではstartCalledが残るため再実行しない。
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
