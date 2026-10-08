#include "RuntimeEntitySnapshot.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

bool Engine::RuntimeEntitySnapshot::Capture(ECSWorld& world, const Entity& root, EntityTreeSnapshot& result) {

	const uint64_t revision = world.GetDataRevision();
	EntityTreeSnapshot candidate;
	EntitySnapshotUtility::CaptureSubtree(world, root, candidate);
	if (candidate.IsEmpty()) {
		return false;
	}
	for (auto& saved : candidate.entities) {
		const Entity entity = world.FindByUUID(saved.stableUUID);
		if (!world.IsAlive(entity)) {
			return false;
		}
		auto scripts = saved.components.find("Script");
		if (scripts == saved.components.end()) {
			continue;
		}
		if (!scripts->is_array()) {
			return false;
		}
		for (auto& script : *scripts) {
			const UUID slot = FromString16Hex(script.value("scriptSlotId", std::string{}));
			bool enabled = script.value("enabled", true);
			auto fields = script.value("serializedFields", nlohmann::json::object());
			// Inspectorの表示値ではなく、保存callback後の値を取得
			if (!BehaviorSystem::CaptureSavedFields(world, entity, slot, fields, enabled)) {
				return false;
			}
			script["serializedFields"] = std::move(fields);
			script["enabled"] = enabled;
		}
	}
	// 異なる時点の階層とComponentを混ぜない
	EntitySnapshotUtility::CaptureReferenceTargets(world, candidate);
	if (world.GetDataRevision() != revision || !world.IsAlive(root)) {
		return false;
	}
	result = std::move(candidate);
	return true;
}
