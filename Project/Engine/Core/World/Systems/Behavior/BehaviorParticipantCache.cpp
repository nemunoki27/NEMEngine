#include "BehaviorParticipantCache.h"

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


void Engine::BehaviorParticipantCache::RebuildParticipants(ECSWorld& world, BehaviorWorld& runtime) {

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
				runtime.FindHandleBySlot(entity, entry.scriptSlotID);
			if (!runtime.IsAlive(handle)) {
				continue;
			}
			const BehaviorRecord* record = runtime.GetRecord(handle);
			if (!record || !record->seen || !record->instance) {
				continue;
			}
			// ProjectSettingsの上書きを反映した型の実行順を使う
			const int32_t executionOrder =
				typeRegistry.GetInfo(record->typeID).executionOrder;
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
	executionOrderRevision_ = typeRegistry.GetExecutionOrderRevision();
}
