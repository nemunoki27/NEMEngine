#include "BehaviorRecordSynchronizer.h"

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


bool Engine::BehaviorRecordSynchronizer::TryResolveTypeID(Engine::ScriptEntry& entry, uint32_t& outTypeID) {

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

Engine::BehaviorRecordSynchronizer::BehaviorRecordSynchronizer(BehaviorWorld& runtime, std::vector<Entity>& dirtyEntities,
	bool& participantsDirty, bool& enableTransitionsDirty) :
	runtime_(runtime), dirtyScriptEntities_(dirtyEntities), participantsDirty_(participantsDirty), enableTransitionsDirty_(enableTransitionsDirty) {}

void Engine::BehaviorRecordSynchronizer::SynchronizeRecords(ECSWorld& world, SystemContext& context, bool sweep) {

	// 全同期はWorld開始、Scene構成変更、Hot Reloadだけで実行する
	runtime_.ClearSeenFlags();
	world.ForEach<ScriptComponent>([&](Entity entity, [[maybe_unused]] ScriptComponent& component) {
		SynchronizeEntityRecords(world, context, entity, false);
		});

	if (sweep) {
		if (runtime_.SweepUnseen(world, context) > 0) {
			participantsDirty_ = true;
		}
	}
	enableTransitionsDirty_ = true;
}

void Engine::BehaviorRecordSynchronizer::SynchronizeDirtyRecords(ECSWorld& world, SystemContext& context) {

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

void Engine::BehaviorRecordSynchronizer::SynchronizeEntityRecords(ECSWorld& world, SystemContext& context,
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

void Engine::BehaviorRecordSynchronizer::QueueScriptEntity(const Entity& entity) {

	if (entity.IsValid()) {
		dirtyScriptEntities_.emplace_back(entity);
	}
	enableTransitionsDirty_ = true;
}
