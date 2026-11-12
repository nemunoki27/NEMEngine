#include "ManagedScriptRuntime.h"

//============================================================================
//	include
//============================================================================
#include "ManagedScriptUtility.h"
#include <Engine/Core/Animation/Timeline/TimelinePlayerAccess.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine;

namespace {

	// 名前変更後も保存した割り当て識別子で参照する
	Engine::UUID ResolveBindingID(const TimelineAsset& asset, std::string_view name) {

		if (const Engine::UUID found = FindTimelineBinding(asset, name)) return found;
		const auto id = TryParseUUID16Hex(name);
		if (id && std::any_of(asset.bindings.begin(), asset.bindings.end(),
			[&](const auto& binding) { return binding.id == *id; })) return *id;
		return {};
	}

	// 所有Worldと読み込み済みアセットを一度に解決する
	std::shared_ptr<const TimelineAsset> ResolveDefinition(ManagedNativeEntity native,
		ECSWorld*& world, Entity& entity, TimelinePlayerRecord*& record) {

		world = ResolveWorld(native);
		entity = ResolveEntity(native);
		record = world && world->IsAlive(entity) && !world->IsPendingDestroy(entity) ? TryGetTimelinePlayer(*world, entity) : nullptr;
		const auto* context = ManagedScriptRuntime::GetCurrentContext();
		if (!record || !context || !context->assetDatabase) return {};
		std::string diagnostic;
		return GetTimelineAsset(*world, *context->assetDatabase, record->settings.timeline, diagnostic);
	}

	// 名前と保存されている型が一致するパラメーターを返す
	const TimelineParameterValue* ResolveValue(ManagedNativeEntity native, const char* name,
		ECSWorld*& world, Entity& entity, TimelinePlayerRecord*& record, Engine::UUID& parameter) {

		if (!name) return nullptr;
		const auto asset = ResolveDefinition(native, world, entity, record);
		if (!asset) return nullptr;
		parameter = FindTimelineParameter(*asset, name);
		if (!parameter) {
			const auto id = TryParseUUID16Hex(name);
			if (id && std::any_of(asset->parameters.begin(), asset->parameters.end(),
				[&](const auto& value) { return value.id == *id; })) parameter = *id;
		}
		return parameter ? GetTimelineParameterValue(*asset, record->settings, parameter) : nullptr;
	}
}

int32_t ManagedScriptRuntime::TimelineCommandCallback(ManagedNativeEntity native, int32_t command, double time) {

	if (command < 1 || command > 6 || !std::isfinite(time) || time < 0) return 0;
	auto* world = ResolveWorld(native);
	const Entity entity = ResolveEntity(native);
	auto* record = world && world->IsAlive(entity) && !world->IsPendingDestroy(entity) ? TryGetTimelinePlayer(*world, entity) : nullptr;
	if (!record) return 0;
	record->runtime.request = command;
	record->runtime.seekTime = time;
	return 1;
}

int32_t ManagedScriptRuntime::GetTimelineStateCallback(ManagedNativeEntity native, ManagedTimelineState* output) {

	if (!output) return 0;
	*output = {};
	auto* world = ResolveWorld(native);
	const Entity entity = ResolveEntity(native);
	const auto* record = world && world->IsAlive(entity) ? TryGetTimelinePlayer(*world, entity) : nullptr;
	if (!record) return 0;
	const auto& state = record->runtime;
	*output = {state.time, state.duration, state.playing ? 1 : 0, state.paused ? 1 : 0, state.finished ? 1 : 0,
		state.preparationProgress, state.preparing ? 1 : 0, static_cast<int32_t>(state.notifications.size())};
	return 1;
}

ManagedNativeEntity ManagedScriptRuntime::GetTimelineBindingCallback(ManagedNativeEntity native, const char* name) {

	if (!name) return {};
	ECSWorld* world = nullptr;
	Entity owner;
	TimelinePlayerRecord* record = nullptr;
	const auto asset = ResolveDefinition(native, world, owner, record);
	if (!asset) return {};
	const Engine::UUID slot = ResolveBindingID(*asset, name);
	if (!slot) return {};
	Entity target = ResolveTimelineBinding(*world, owner, record->settings, slot);
	const bool assigned = std::any_of(record->settings.bindings.begin(), record->settings.bindings.end(),
		[&](const auto& binding) { return binding.slot == slot; });
	const auto definition = std::find_if(asset->bindings.begin(), asset->bindings.end(),
		[&](const auto& binding) { return binding.id == slot; });
	if (!assigned && definition != asset->bindings.end() && definition->name == "Actor") target = owner;
	return MakeNativeEntity(*world, target);
}

int32_t ManagedScriptRuntime::SetTimelineBindingCallback(ManagedNativeEntity native, const char* name,
	ManagedNativeEntity targetNative) {

	if (!name) return 0;
	ECSWorld* world = nullptr;
	Entity owner;
	TimelinePlayerRecord* record = nullptr;
	const auto asset = ResolveDefinition(native, world, owner, record);
	if (!asset) return 0;
	const Engine::UUID slot = ResolveBindingID(*asset, name);
	if (!slot) return 0;
	Entity target = Entity::Null();
	if (targetNative.IsValid()) {
		target = ResolveEntity(targetNative);
		if (ResolveWorld(targetNative) != world || !world->IsAlive(target) || world->IsPendingDestroy(target)) return 0;
		const auto definition = std::find_if(asset->bindings.begin(), asset->bindings.end(),
			[&](const auto& binding) { return binding.id == slot; });
		if (definition != asset->bindings.end() && !definition->component.empty() &&
			!HasTimelineBindingComponent(*world, target, definition->component)) return 0;
	}
	auto settings = record->settings;
	const auto found = std::find_if(settings.bindings.begin(), settings.bindings.end(),
		[&](const auto& binding) { return binding.slot == slot; });
	TimelinePlayerBinding binding{slot};
	if (world->IsAlive(target)) {
		binding.target = SceneEntityReference::FromEntity(*world, target);
		binding.runtimeTargetID = world->GetUUID(target);
	}
	if (found == settings.bindings.end()) settings.bindings.push_back(binding);
	else *found = binding;
	SetTimelinePlayerSettings(*world, owner, settings);
	// 保存形式への変換で消える実行時の参照を明示的な設定時だけ更新する
	if (auto* updated = TryGetTimelinePlayer(*world, owner)) {
		for (auto& current : updated->settings.bindings)
			if (current.slot == slot) { current.runtimeTargetID = binding.runtimeTargetID; break; }
	}
	return 1;
}

int32_t ManagedScriptRuntime::GetTimelineParameterCallback(ManagedNativeEntity native, const char* name,
	ManagedTimelineValue* output) {

	if (!output) return 0;
	*output = {};
	ECSWorld* world = nullptr;
	Entity owner;
	TimelinePlayerRecord* record = nullptr;
	Engine::UUID parameter;
	const auto* value = ResolveValue(native, name, world, owner, record, parameter);
	if (!value || value->index() > 3) return 0;
	output->kind = static_cast<int32_t>(value->index());
	if (const auto* boolean = std::get_if<bool>(value)) output->integer = *boolean ? 1 : 0;
	else if (const auto* integer = std::get_if<int32_t>(value)) output->integer = *integer;
	else if (const auto* scalar = std::get_if<float>(value)) output->scalar = *scalar;
	else if (const auto* vector = std::get_if<Vector3>(value)) {
		output->x = vector->x; output->y = vector->y; output->z = vector->z;
	}
	return 1;
}

int32_t ManagedScriptRuntime::SetTimelineParameterCallback(ManagedNativeEntity native, const char* name,
	const ManagedTimelineValue* input) {

	if (!input || input->kind < 0 || input->kind > 3) return 0;
	ECSWorld* world = nullptr;
	Entity owner;
	TimelinePlayerRecord* record = nullptr;
	Engine::UUID parameter;
	const auto* previous = ResolveValue(native, name, world, owner, record, parameter);
	if (!previous || previous->index() != static_cast<size_t>(input->kind)) return 0;
	TimelineParameterValue value;
	if (input->kind == 0) value = input->integer != 0;
	else if (input->kind == 1) value = input->integer;
	else if (input->kind == 2) {
		if (!std::isfinite(input->scalar)) return 0;
		value = input->scalar;
	} else {
		if (!std::isfinite(input->x) || !std::isfinite(input->y) || !std::isfinite(input->z)) return 0;
		value = Vector3{input->x, input->y, input->z};
	}
	SetTimelineParameterValue(record->settings, parameter, value);
	return 1;
}

int32_t ManagedScriptRuntime::IsTimelineWindowOpenCallback(ManagedNativeEntity native, const char* name) {

	if (!name) return 0;
	auto* world = ResolveWorld(native);
	const Entity entity = ResolveEntity(native);
	const auto* record = world && world->IsAlive(entity) ? TryGetTimelinePlayer(*world, entity) : nullptr;
	return record && record->runtime.windows.contains(name) ? 1 : 0;
}

int32_t ManagedScriptRuntime::CopyTimelineStringParameterCallback(ManagedNativeEntity native, const char* name,
	char* output, int32_t capacity) {

	ECSWorld* world = nullptr;
	Entity owner;
	TimelinePlayerRecord* record = nullptr;
	Engine::UUID parameter;
	const auto* value = ResolveValue(native, name, world, owner, record, parameter);
	const auto* text = value ? std::get_if<std::string>(value) : nullptr;
	return text ? CopyStringToBuffer(*text, output, capacity) : -1;
}

int32_t ManagedScriptRuntime::SetTimelineStringParameterCallback(ManagedNativeEntity native, const char* name,
	const char* text) {

	if (!text || std::char_traits<char>::length(text) > 1024 * 1024) return 0;
	ECSWorld* world = nullptr;
	Entity owner;
	TimelinePlayerRecord* record = nullptr;
	Engine::UUID parameter;
	const auto* previous = ResolveValue(native, name, world, owner, record, parameter);
	if (!previous || !std::holds_alternative<std::string>(*previous)) return 0;
	SetTimelineParameterValue(record->settings, parameter, std::string(text));
	return 1;
}
