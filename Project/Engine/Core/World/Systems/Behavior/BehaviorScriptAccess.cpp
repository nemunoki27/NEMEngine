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


void Engine::BehaviorSystem::DispatchCollisionEnter(ECSWorld& world,
	SystemContext& context, const CollisionContact& collision) {

	// アクティブなBehaviorSystemへ衝突開始を渡す
	if (activeSystem_) {
		activeSystem_->session_.DispatchCollision(world, context, collision, 0);
	}
}

void Engine::BehaviorSystem::DispatchCollisionStay(ECSWorld& world,
	SystemContext& context, const CollisionContact& collision) {

	// アクティブなBehaviorSystemへ衝突継続を渡す
	if (activeSystem_) {
		activeSystem_->session_.DispatchCollision(world, context, collision, 1);
	}
}

void Engine::BehaviorSystem::DispatchCollisionExit(ECSWorld& world,
	SystemContext& context, const CollisionContact& collision) {

	// アクティブなBehaviorSystemへ衝突終了を渡す
	if (activeSystem_) {
		activeSystem_->session_.DispatchCollision(world, context, collision, 2);
	}
}

void Engine::BehaviorSystem::DispatchAnimationEvent(ECSWorld& world, SystemContext& context, const Entity& entity,
	const std::string& name, float floatParam, int32_t intParam, const std::string& stringParam) {

	if (!activeSystem_ || context.mode != WorldMode::Play || activeSystem_->session_.activeWorld_ != &world) {
		return;
	}

	// 実行順を保ったスナップショットから対象Entityだけへ通知する
	const std::vector<BehaviorParticipantCache::SyncParticipant> participants = activeSystem_->session_.participantCache_.participants_;
	for (const BehaviorParticipantCache::SyncParticipant& participant : participants) {

		if (participant.owner != entity) {
			continue;
		}
		BehaviorRecord* record = activeSystem_->session_.runtime_.GetRecord(participant.handle);
		if (!record || !activeSystem_->session_.CanInvokeParticipant(world, participant, *record)) {
			continue;
		}
		record->instance->OnAnimationEvent(world, context, entity, name, floatParam, intParam, stringParam);
		activeSystem_->session_.RefreshFaultState(participant.handle);
		activeSystem_->session_.SynchronizeLifecycleIfDirty(world, context);
	}
}

void Engine::BehaviorSystem::SynchronizeInstantiatedEntities(ECSWorld& world, SystemContext& context,
	std::span<const Entity> entities) {

	if (!activeSystem_ || context.mode != WorldMode::Play ||
		activeSystem_->session_.activeWorld_ != &world || entities.empty()) {
		return;
	}

	// Prefab追加通知を通常同期へ残すと返却後にSerializeFieldを再適用するため先に消費する
	auto& dirtyEntities = activeSystem_->session_.dirtyScriptEntities_;
	dirtyEntities.erase(std::remove_if(dirtyEntities.begin(), dirtyEntities.end(),
		[entities](const Entity& dirty) {

			return std::find(entities.begin(), entities.end(), dirty) != entities.end();
		}), dirtyEntities.end());

	// 全Script実体を先に作りPrefab内参照をAwakeより前に解決できる状態へ揃える
	for (const Entity& entity : entities) {

		if (world.IsAlive(entity) && world.HasComponent<ScriptComponent>(entity)) {
			activeSystem_->session_.records_.SynchronizeEntityRecords(world, context, entity, true);
		}
	}
	ManagedScriptRuntime::GetInstance().FlushPendingReferences(world);
	activeSystem_->session_.participantCache_.participantsDirty_ = true;
	activeSystem_->session_.enableTransitionsDirty_ = true;
	activeSystem_->session_.participantCache_.RebuildParticipants(world, activeSystem_->session_.runtime_);
	activeSystem_->session_.participantCache_.participantsDirty_ = false;
	activeSystem_->session_.FlushActiveTransitions(world, context);

	// Startは呼び出し元のコールバック終了後に通常Lifecycle同期で実行する
	activeSystem_->session_.participantCache_.participantsDirty_ = true;
}

nlohmann::json Engine::BehaviorSystem::GetRuntimeSerializedState(BehaviorHandle handle) {

	// Play中のinstanceの現在値を返す、無効なら空
	if (!activeSystem_ || !activeSystem_->session_.runtime_.IsAlive(handle)) {
		return nlohmann::json::object();
	}
	BehaviorRecord* record = activeSystem_->session_.runtime_.GetRecord(handle);
	if (!record || !record->instance) {
		return nlohmann::json::object();
	}
	return record->instance->GetRuntimeSerializedState();
}

void Engine::BehaviorSystem::SetRuntimeSerializedField(BehaviorHandle handle,
	const std::string& fieldID, const nlohmann::json& value) {

	if (!activeSystem_ || !activeSystem_->session_.runtime_.IsAlive(handle)) {
		return;
	}
	BehaviorRecord* record = activeSystem_->session_.runtime_.GetRecord(handle);
	if (record && record->instance) {
		record->instance->SetRuntimeSerializedField(*activeSystem_->session_.activeWorld_, fieldID, value);
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

	if (!activeSystem_ || !activeSystem_->session_.activeWorld_) {
		return -1;
	}
	ScriptEntry* entry = FindScriptEntryBySlot(*activeSystem_->session_.activeWorld_, owner, scriptSlotID);
	if (!entry) {
		return -1;
	}
	// runtime overrideがあればそれを、無ければauthoringのenabledを返す
	const BehaviorHandle handle =
		activeSystem_->session_.runtime_.FindHandleBySlot(owner, scriptSlotID);
	if (BehaviorRecord* record = activeSystem_->session_.runtime_.GetRecord(handle)) {
		if (record->hasRuntimeEnabledOverride) {
			return record->runtimeEnabledOverride ? 1 : 0;
		}
	}
	return entry->enabled ? 1 : 0;
}

void Engine::BehaviorSystem::SetScriptEnabled(const Entity& owner, const UUID& scriptSlotID, bool enabled) {

	if (!activeSystem_ || !activeSystem_->session_.activeWorld_) {
		return;
	}
	ScriptEntry* entry = FindScriptEntryBySlot(*activeSystem_->session_.activeWorld_, owner, scriptSlotID);
	if (!entry) {
		return;
	}
	// runtime overrideだけ立て、authoringのenabledは変更しない、次のsyncで反映される
	const BehaviorHandle handle =
		activeSystem_->session_.runtime_.FindHandleBySlot(owner, scriptSlotID);
	if (BehaviorRecord* record = activeSystem_->session_.runtime_.GetRecord(handle)) {
		record->runtimeEnabledOverride = enabled;
		record->hasRuntimeEnabledOverride = true;
		activeSystem_->session_.enableTransitionsDirty_ = true;
	}
}

Engine::BehaviorHandle Engine::BehaviorSystem::FindRuntimeHandle(
	const Entity& owner, const UUID& scriptSlotID) {

	if (!activeSystem_ || !activeSystem_->session_.activeWorld_) {
		return BehaviorHandle::Null();
	}
	return activeSystem_->session_.runtime_.FindHandleBySlot(owner, scriptSlotID);
}

Engine::MonoBehavior* Engine::BehaviorSystem::FindScriptInstance(const Entity& owner, const std::string& scriptTypeID) {

	if (!activeSystem_ || !activeSystem_->session_.activeWorld_) {
		return nullptr;
	}

	// owner上でscriptTypeID一致のScriptEntryを探しinstanceを返す
	if (!activeSystem_->session_.activeWorld_->HasComponent<ScriptComponent>(owner)) {
		return nullptr;
	}
	for (ScriptEntry& entry :
		GetScriptEntries(*activeSystem_->session_.activeWorld_, owner)) {
		if (entry.scriptTypeID != scriptTypeID) {
			continue;
		}
		const BehaviorHandle handle =
			activeSystem_->session_.runtime_.FindHandleBySlot(owner, entry.scriptSlotID);
		if (BehaviorRecord* record = activeSystem_->session_.runtime_.GetRecord(handle)) {
			if (record->instance) {
				return record->instance.get();
			}
		}
	}
	return nullptr;
}

bool Engine::BehaviorSystem::AttachScript(const Entity& owner, const std::string& scriptTypeID) {

	if (!activeSystem_ || !activeSystem_->session_.activeWorld_ || scriptTypeID.empty()) {
		return false;
	}
	ECSWorld& world = *activeSystem_->session_.activeWorld_;
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
	const BehaviorHandle handle = activeSystem_->session_.runtime_.Create(
		info->id, owner, entry.scriptSlotID);

	BehaviorRecord* record = activeSystem_->session_.runtime_.GetRecord(handle);
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
	activeSystem_->session_.participantCache_.participantsDirty_ = true;
	activeSystem_->session_.enableTransitionsDirty_ = true;
	return true;
}
