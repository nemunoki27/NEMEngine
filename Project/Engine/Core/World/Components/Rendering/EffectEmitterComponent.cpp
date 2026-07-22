#include "EffectEmitterComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <unordered_set>

//============================================================================
//	EffectEmitterComponent internal
//============================================================================
namespace {

	Engine::EffectEmitterState LoadState(const nlohmann::json& in) {

		Engine::EffectEmitterState state{};
		state.id = Engine::FromString16Hex(in.value("id", std::string()));
		if (!state.id) { state.id = Engine::UUID::New(); }
		state.name = in.value("name", state.name);
		state.enabled = in.value("enabled", state.enabled);
		state.effect = Engine::ParseAssetID(in, "effect");
		state.mode = Engine::EnumAdapter<Engine::EffectEmitterMode>::FromString(
			in.value("mode", "Continuous")).value_or(Engine::EffectEmitterMode::Continuous);
		state.delay = (std::max)(in.value("delay", state.delay), 0.0f);
		state.count = (std::max)(in.value("count", state.count), 1);
		state.interval = (std::max)(in.value("interval", state.interval), 0.0f);
		state.duration = (std::max)(in.value("duration", state.duration), 0.0f);
		state.emitUntilStopped = in.value("emitUntilStopped", state.duration <= 0.0f);
		if (const auto it = in.find("localPosition"); it != in.end()) {
			state.localPosition = Engine::Vector3::FromJson(*it);
		}
		if (const auto it = in.find("localRotation"); it != in.end()) {
			state.localRotation = Engine::Quaternion::FromJson(*it);
		}
		if (const auto it = in.find("localScale"); it != in.end()) {
			state.localScale = Engine::Vector3::FromJson(*it);
		}
		if (const auto it = in.find("parentSettings"); it != in.end() && it->is_object()) {

			state.parentSettings.useEmitter = it->value("useEmitter", false);
			state.parentSettings.ignoreParentRotation = it->value("ignoreParentRotation", false);
			state.parentSettings.ignoreParentScale = it->value("ignoreParentScale", true);
			state.parentSettings.keepWorldOnDetach = it->value("keepWorldOnDetach", true);
			const std::string localFileID = it->value("entityLocalFileID", "");
			state.parentSettings.entityLocalFileID = localFileID.empty() ?
				Engine::UUID{} : Engine::FromString16Hex(localFileID);
			if (state.parentSettings.useEmitter) { state.parentSettings.entityLocalFileID = {}; }
		}
		return state;
	}

	nlohmann::json SaveState(const Engine::EffectEmitterState& state) {

		nlohmann::json out{};
		out["id"] = Engine::ToString(state.id);
		out["name"] = state.name;
		out["enabled"] = state.enabled;
		out["effect"] = Engine::ToAssetReferenceJson(state.effect);
		out["mode"] = Engine::EnumAdapter<Engine::EffectEmitterMode>::ToString(state.mode);
		out["delay"] = state.delay;
		out["count"] = state.count;
		out["interval"] = state.interval;
		out["duration"] = state.duration;
		out["emitUntilStopped"] = state.emitUntilStopped;
		out["localPosition"] = state.localPosition.ToJson();
		out["localRotation"] = state.localRotation.ToJson();
		out["localScale"] = state.localScale.ToJson();
		out["parentSettings"] = nlohmann::json::object();
		out["parentSettings"]["useEmitter"] = state.parentSettings.useEmitter;
		out["parentSettings"]["entityLocalFileID"] = state.parentSettings.entityLocalFileID ?
			Engine::ToString(state.parentSettings.entityLocalFileID) : "";
		out["parentSettings"]["ignoreParentRotation"] = state.parentSettings.ignoreParentRotation;
		out["parentSettings"]["ignoreParentScale"] = state.parentSettings.ignoreParentScale;
		out["parentSettings"]["keepWorldOnDetach"] = state.parentSettings.keepWorldOnDetach;
		return out;
	}

	Engine::EffectEmitterGroup LoadGroup(const nlohmann::json& in) {

		Engine::EffectEmitterGroup group{};
		group.name = in.value("name", group.name);
		group.states.clear();
		if (const auto it = in.find("states"); it != in.end() && it->is_array()) {
			for (const nlohmann::json& state : *it) {
				group.states.emplace_back(LoadState(state));
			}
		}
		return group;
	}

	nlohmann::json SaveGroup(const Engine::EffectEmitterGroup& group) {

		nlohmann::json out{};
		out["name"] = group.name;
		out["states"] = nlohmann::json::array();
		for (const Engine::EffectEmitterState& state : group.states) {
			out["states"].emplace_back(SaveState(state));
		}
		return out;
	}

	void ResetRuntime(Engine::EffectEmitterComponent& component) {

		component.runtimeStarted = false;
		component.runtimeNextPlaybackID = 1;
		component.runtimeNextEffectInstanceID = 1;
		component.runtimePlaybacks.clear();
		component.runtimeCommands.clear();
	}
}

//============================================================================
//	EffectEmitterComponent structMethods
//============================================================================
uint64_t Engine::EffectEmitterComponent::Emit(std::string_view groupName) {

	if (!enabled) { return 0; }
	const std::string resolvedName = groupName.empty() ? defaultGroup : std::string(groupName);
	const auto found = std::find_if(groups.begin(), groups.end(), [&](const EffectEmitterGroup& group) {
		return group.name == resolvedName;
		});
	if (found == groups.end() || std::none_of(found->states.begin(), found->states.end(),
		[](const EffectEmitterState& state) { return state.enabled; })) {
		return 0;
	}

	EffectEmitterCommand command{};
	command.playbackID = runtimeNextPlaybackID++;
	if (command.playbackID == 0) { command.playbackID = runtimeNextPlaybackID++; }
	command.groupName = resolvedName;
	runtimeCommands.emplace_back(std::move(command));
	return runtimeCommands.back().playbackID;
}

uint64_t Engine::EffectEmitterComponent::EmitAt(std::string_view groupName,
	const Vector3& position, const Quaternion& rotation) {

	const uint64_t playbackID = Emit(groupName);
	if (playbackID == 0) { return 0; }
	EffectEmitterCommand& command = runtimeCommands.back();
	command.fixedAnchor = true;
	command.position = position;
	command.rotation = Quaternion::Length(rotation) <= 1.0e-6f ?
		Quaternion::Identity() : Quaternion::Normalize(rotation);
	return playbackID;
}

void Engine::EffectEmitterComponent::Stop(uint64_t playbackID) {

	EffectEmitterCommand command{};
	command.type = EffectEmitterCommandType::StopHandle;
	command.playbackID = playbackID;
	runtimeCommands.emplace_back(std::move(command));
}

void Engine::EffectEmitterComponent::Stop(std::string_view groupName) {

	EffectEmitterCommand command{};
	command.type = EffectEmitterCommandType::StopGroup;
	command.groupName = groupName;
	runtimeCommands.emplace_back(std::move(command));
}

void Engine::EffectEmitterComponent::Stop() {

	EffectEmitterCommand command{};
	command.type = EffectEmitterCommandType::StopAll;
	runtimeCommands.emplace_back(std::move(command));
}

void Engine::EffectEmitterComponent::Clear(uint64_t playbackID) {

	EffectEmitterCommand command{};
	command.type = EffectEmitterCommandType::ClearHandle;
	command.playbackID = playbackID;
	runtimeCommands.emplace_back(std::move(command));
}

void Engine::EffectEmitterComponent::Clear(std::string_view groupName) {

	EffectEmitterCommand command{};
	command.type = EffectEmitterCommandType::ClearGroup;
	command.groupName = groupName;
	runtimeCommands.emplace_back(std::move(command));
}

void Engine::EffectEmitterComponent::Clear() {

	EffectEmitterCommand command{};
	command.type = EffectEmitterCommandType::ClearAll;
	runtimeCommands.emplace_back(std::move(command));
}

bool Engine::EffectEmitterComponent::IsPlaying(uint64_t playbackID) const {

	return std::any_of(runtimePlaybacks.begin(), runtimePlaybacks.end(), [&](const EffectEmitterPlaybackRuntime& playback) {
		return playback.id == playbackID;
		}) || std::any_of(runtimeCommands.begin(), runtimeCommands.end(), [&](const EffectEmitterCommand& command) {
		return command.type == EffectEmitterCommandType::Emit && command.playbackID == playbackID;
		});
}

bool Engine::EffectEmitterComponent::IsPlaying(std::string_view groupName) const {

	return std::any_of(runtimePlaybacks.begin(), runtimePlaybacks.end(), [&](const EffectEmitterPlaybackRuntime& playback) {
		return playback.groupName == groupName;
		}) || std::any_of(runtimeCommands.begin(), runtimeCommands.end(), [&](const EffectEmitterCommand& command) {
		return command.type == EffectEmitterCommandType::Emit && command.groupName == groupName;
		});
}

void Engine::from_json(const nlohmann::json& in, EffectEmitterComponent& component) {

	component.enabled = in.value("enabled", true);
	component.defaultGroup = in.value("defaultGroup", std::string("Default"));
	component.playOnStart = in.value("playOnStart", in.value("playing", true));
	component.playInEditMode = in.value("playInEditMode", true);
	component.drawEmitterShape = in.value("drawEmitterShape", false);
	component.layer = in.value("layer", 0);
	component.order = in.value("order", 0);
	component.visible = in.value("visible", true);

	component.groups.clear();
	if (const auto it = in.find("groups"); it != in.end() && it->is_array()) {
		for (const nlohmann::json& group : *it) {
			component.groups.emplace_back(LoadGroup(group));
		}
	} else {

		// 旧ParticleEmitterは単一アセットのContinuous設定へ移行する
		EffectEmitterGroup group{};
		group.name = "Default";
		group.states.front().effect = ParseAssetID(in, "effect");
		component.groups.emplace_back(std::move(group));
		component.defaultGroup = "Default";
	}

	// state IDはコンポーネント内で一意にする
	std::unordered_set<UUID> stateIDs{};
	for (EffectEmitterGroup& group : component.groups) {
		for (EffectEmitterState& state : group.states) {
			while (!state.id || stateIDs.contains(state.id)) { state.id = UUID::New(); }
			stateIDs.emplace(state.id);
		}
	}
	ResetRuntime(component);
}

void Engine::to_json(nlohmann::json& out, const EffectEmitterComponent& component) {

	out["enabled"] = component.enabled;
	out["defaultGroup"] = component.defaultGroup;
	out["playOnStart"] = component.playOnStart;
	out["playInEditMode"] = component.playInEditMode;
	out["drawEmitterShape"] = component.drawEmitterShape;
	out["layer"] = component.layer;
	out["order"] = component.order;
	out["visible"] = component.visible;
	out["groups"] = nlohmann::json::array();
	for (const EffectEmitterGroup& group : component.groups) {
		out["groups"].emplace_back(SaveGroup(group));
	}
}
