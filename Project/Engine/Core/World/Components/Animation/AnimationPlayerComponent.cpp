#include "AnimationPlayerComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	AnimationPlayerComponent classMethods
//============================================================================
namespace {

	// stateのループ繋ぎ補間をjsonへ書き出す
	nlohmann::json SaveLoopBridge(const Engine::AnimationLoopBridgeSettings& bridge) {

		nlohmann::json out = nlohmann::json::object();
		out["enabled"] = bridge.enabled;
		out["duration"] = bridge.duration;
		out["interpolation"] = Engine::ToString(bridge.interpolation);
		return out;
	}

	// jsonからstateのループ繋ぎ補間を読み込む
	Engine::AnimationLoopBridgeSettings LoadLoopBridge(const nlohmann::json& in) {

		Engine::AnimationLoopBridgeSettings bridge{};
		if (!in.is_object()) {
			return bridge;
		}
		bridge.enabled = in.value("enabled", bridge.enabled);
		bridge.duration = (std::max)(in.value("duration", bridge.duration), 0.001f);
		Engine::CurveInterpolationMode interpolation = bridge.interpolation;
		if (Engine::TryParseCurveInterpolationMode(in.value("interpolation", "Linear"), interpolation)) {
			bridge.interpolation = interpolation;
		}
		return bridge;
	}

	// json1要素からstateを読み込む
	Engine::AnimationState LoadState(const nlohmann::json& in) {

		Engine::AnimationState state{};
		state.name = in.value("name", std::string());
		state.clip = Engine::ParseAssetID(in, "clip");
		state.speed = in.value("speed", 1.0f);
		state.wrapMode = Engine::EnumAdapter<Engine::AnimationWrapMode>::FromString(
			in.value("wrapMode", "UseClip")).value_or(Engine::AnimationWrapMode::UseClip);
		state.relativeTransform = in.value("relativeTransform", false);
		if (const auto it = in.find("loopBridge"); it != in.end()) {
			state.loopBridge = LoadLoopBridge(*it);
		}
		state.loopCount = in.value("loopCount", 0);
		state.pingPongCount = in.value("pingPongCount", 0);
		state.startDelay = in.value("startDelay", 0.0f);
		state.interval = in.value("interval", 0.0f);
		return state;
	}

	// stateをjson1要素へ書き出す
	nlohmann::json SaveState(const Engine::AnimationState& state) {

		nlohmann::json out;
		out["name"] = state.name;
		out["clip"] = Engine::ToAssetReferenceJson(state.clip);
		out["speed"] = state.speed;
		out["wrapMode"] = Engine::EnumAdapter<Engine::AnimationWrapMode>::ToString(state.wrapMode);
		out["relativeTransform"] = state.relativeTransform;
		out["loopBridge"] = SaveLoopBridge(state.loopBridge);
		out["loopCount"] = state.loopCount;
		out["pingPongCount"] = state.pingPongCount;
		out["startDelay"] = state.startDelay;
		out["interval"] = state.interval;
		return out;
	}

	// jsonからgroup1要素を読み込む
	Engine::AnimationGroup LoadGroup(const nlohmann::json& in) {

		Engine::AnimationGroup group{};
		group.name = in.value("name", std::string());
		if (in.contains("states") && in["states"].is_array()) {
			for (const auto& stateJson : in["states"]) {
				group.states.push_back(LoadState(stateJson));
			}
		}
		return group;
	}

	// groupをjson1要素へ書き出す
	nlohmann::json SaveGroup(const Engine::AnimationGroup& group) {

		nlohmann::json out;
		out["name"] = group.name;
		out["states"] = nlohmann::json::array();
		for (const auto& state : group.states) {
			out["states"].push_back(SaveState(state));
		}
		return out;
	}
}

void Engine::from_json(const nlohmann::json& in, AnimationPlayerComponent& component) {

	component.enabled = in.value("enabled", true);
	component.defaultGroup = in.value("defaultGroup", std::string());
	component.playOnStart = in.value("playOnStart", true);
	component.playInEditMode = in.value("playInEditMode", false);
	component.globalSpeed = in.value("globalSpeed", 1.0f);

	component.groups.clear();
	if (in.contains("groups") && in["groups"].is_array()) {
		for (const auto& groupJson : in["groups"]) {
			component.groups.push_back(LoadGroup(groupJson));
		}
	}

	// ランタイム状態は保存データから復元しない
	component.runtimeCurrentGroup.clear();
	component.runtimeCurrentClips.clear();
	component.runtimeFromGroup.clear();
	component.runtimeFromClips.clear();
	component.runtimeFade = 0.0f;
	component.runtimeFadeDuration = 0.0f;
	component.runtimeInTransition = false;
	component.runtimeCurrent.clear();
	component.runtimeRepeatCount = 0;
	component.runtimePlaying = false;
	component.runtimeFinished = false;
	component.runtimeStarted = false;
	component.runtimeBaseCaptured = false;
	component.runtimeBaseValues.clear();
	component.runtimePlayRequest.clear();
	component.runtimePlayFade = 0.0f;
	component.runtimeStopRequest = false;
}

void Engine::to_json(nlohmann::json& out, const AnimationPlayerComponent& component) {

	out["enabled"] = component.enabled;
	out["defaultGroup"] = component.defaultGroup;
	out["playOnStart"] = component.playOnStart;
	out["playInEditMode"] = component.playInEditMode;
	out["globalSpeed"] = component.globalSpeed;

	out["groups"] = nlohmann::json::array();
	for (const auto& group : component.groups) {
		out["groups"].push_back(SaveGroup(group));
	}
}
