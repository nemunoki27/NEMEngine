#include "ManagedTimelineSettings.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include <Engine/Core/World/Components/Animation/TimelinePlayerComponent.h>

using namespace Engine;

bool Engine::ReadTimelinePlayerSettings(ECSWorld& world, Entity entity, nlohmann::json& data) {

	const auto* record = TryGetTimelinePlayer(world, entity);
	if (!record) return false;
	data = record->settings;
	const auto& runtime = record->runtime;
	data["runtime"] = {{"time", runtime.time}, {"duration", runtime.duration}, {"isPlaying", runtime.playing},
		{"isPaused", runtime.paused}, {"isFinished", runtime.finished}, {"cycles", runtime.cycles},
		{"diagnostic", runtime.diagnostic}};
	data["events"] = nlohmann::json::array();
	for (const auto& event : runtime.notifications) {
		const auto target = MakeNativeEntity(world, event.target);
		data["events"].push_back({{"name", event.name}, {"time", event.time}, {"floatParam", event.floatParam},
			{"intParam", event.intParam}, {"stringParam", event.stringParam}, {"targetNative", {
				{"world", {{"index", target.world.index}, {"generation", target.world.generation}}},
				{"index", target.index}, {"generation", target.generation}}}});
	}
	data["windows"] = runtime.windows;
	return true;
}

bool Engine::ApplyTimelinePlayerSettings(ECSWorld& world, Entity entity, const nlohmann::json& data) {

	auto* record = TryGetTimelinePlayer(world, entity);
	if (!record) return false;
	if (data.value("consumeEvents", false)) {
		record->runtime.notifications.clear();
		return true;
	}
	nlohmann::json merged = record->settings;
	merged.merge_patch(data);
	SetTimelinePlayerSettings(world, entity, merged.get<TimelinePlayerSettings>());
	return true;
}
