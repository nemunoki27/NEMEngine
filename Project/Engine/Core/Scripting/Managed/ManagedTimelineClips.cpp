#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include <Engine/Core/World/ECS/World/ECSWorld.h>

bool Engine::ManagedScriptRuntime::CreateTimelineClip(ECSWorld& world, Entity owner, const SystemContext& context, std::string_view typeName,
	std::string_view configuration, uint64_t& handle) {

	handle = 0;
	if (!initialized_ || !bridge_.createTimelineClip_ || !world.IsAlive(owner)) return false;
	ScopedInvocationContext scope(context);
	const std::string name(typeName);
	return bridge_.createTimelineClip_(MakeNativeEntity(world, owner), name.c_str(), configuration.data(),
		static_cast<int32_t>(configuration.size()), &handle) == ManagedStatus::Ok && handle != 0;
}

bool Engine::ManagedScriptRuntime::EvaluateTimelineClip(uint64_t handle, const SystemContext& context,
	const ManagedTimelineFrame& frame, const ManagedTimelineBindingUpdate* bindings, int32_t bindingCount,
	const ManagedTimelineParameterUpdate* parameters, int32_t parameterCount) {

	if (!initialized_ || !bridge_.evaluateTimelineClip_ || !handle) return false;
	ScopedInvocationContext scope(context);
	if ((bindingCount || parameterCount) && (!bridge_.updateTimelineClipContext_ ||
		bridge_.updateTimelineClipContext_(handle, bindings, bindingCount, parameters, parameterCount) != ManagedStatus::Ok)) return false;
	return bridge_.evaluateTimelineClip_(handle, &frame) == ManagedStatus::Ok;
}

bool Engine::ManagedScriptRuntime::UpdateTimelineClipConfiguration(uint64_t handle, const SystemContext& context,
	std::string_view typeName, std::string_view configuration) {

	if (!initialized_ || !bridge_.updateTimelineClipConfiguration_ || !handle) return false;
	ScopedInvocationContext scope(context);
	const std::string name(typeName);
	return bridge_.updateTimelineClipConfiguration_(handle, name.c_str(), configuration.data(),
		static_cast<int32_t>(configuration.size())) == ManagedStatus::Ok;
}

void Engine::ManagedScriptRuntime::ReleaseTimelineClip(uint64_t handle, const SystemContext& context, int32_t reason) {

	if (!initialized_ || !bridge_.releaseTimelineClip_ || !handle) return;
	ScopedInvocationContext scope(context);
	bridge_.releaseTimelineClip_(handle, reason);
}

std::string Engine::ManagedScriptRuntime::CopyTimelineClipTypes() {

	if (!initialized_ || !bridge_.copyTimelineClipTypes_) return "[]";
	const int32_t size = bridge_.copyTimelineClipTypes_(nullptr, 0);
	if (size <= 0 || size > 16 * 1024 * 1024) return "[]";
	std::string output(static_cast<size_t>(size) + 1, '\0');
	const int32_t written = bridge_.copyTimelineClipTypes_(output.data(), static_cast<int32_t>(output.size()));
	if (written < 0 || written > size) return "[]";
	output.resize(static_cast<size_t>(written));
	return output;
}
