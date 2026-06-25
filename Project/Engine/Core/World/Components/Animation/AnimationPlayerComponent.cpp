#include "AnimationPlayerComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	AnimationPlayerComponent classMethods
//============================================================================
namespace {

	// json1要素からstateを読み込む
	Engine::AnimationState LoadState(const nlohmann::json& in) {

		Engine::AnimationState state{};
		state.name = in.value("name", std::string());
		state.clip = Engine::ParseAssetID(in, "clip");
		state.speed = in.value("speed", 1.0f);
		state.wrapMode = Engine::EnumAdapter<Engine::AnimationWrapMode>::FromString(
			in.value("wrapMode", "UseClip")).value_or(Engine::AnimationWrapMode::UseClip);
		return state;
	}

	// stateをjson1要素へ書き出す
	nlohmann::json SaveState(const Engine::AnimationState& state) {

		nlohmann::json out;
		out["name"] = state.name;
		out["clip"] = Engine::ToAssetReferenceJson(state.clip);
		out["speed"] = state.speed;
		out["wrapMode"] = Engine::EnumAdapter<Engine::AnimationWrapMode>::ToString(state.wrapMode);
		return out;
	}
}

void Engine::from_json(const nlohmann::json& in, AnimationPlayerComponent& component) {

	component.enabled = in.value("enabled", true);
	component.defaultState = in.value("defaultState", std::string());
	component.playOnStart = in.value("playOnStart", true);
	component.playInEditMode = in.value("playInEditMode", false);
	component.globalSpeed = in.value("globalSpeed", 1.0f);

	component.states.clear();
	if (in.contains("states") && in["states"].is_array()) {
		for (const auto& stateJson : in["states"]) {
			component.states.push_back(LoadState(stateJson));
		}
	}

	// ランタイム状態は保存データから復元しない
	component.runtimeCurrent.clear();
	component.runtimeFrom.clear();
	component.runtimeTo.clear();
	component.runtimeTime = 0.0f;
	component.runtimeFromTime = 0.0f;
	component.runtimeFade = 0.0f;
	component.runtimeFadeDuration = 0.0f;
	component.runtimeDir = 1;
	component.runtimeFromDir = 1;
	component.runtimePlaying = false;
	component.runtimeInTransition = false;
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
	out["defaultState"] = component.defaultState;
	out["playOnStart"] = component.playOnStart;
	out["playInEditMode"] = component.playInEditMode;
	out["globalSpeed"] = component.globalSpeed;

	out["states"] = nlohmann::json::array();
	for (const auto& state : component.states) {
		out["states"].push_back(SaveState(state));
	}
}
