#include "AudioSourceComponent.h"

//============================================================================
//	AudioSourceComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, AudioSourceComponent& component) {

	component.clip = ParseAssetID(in, "clip");
	component.enabled = in.value("enabled", component.enabled);
	component.playOnAwake = in.value("playOnAwake", component.playOnAwake);
	component.loop = in.value("loop", component.loop);
	component.volume = in.value("volume", component.volume);

	// Runtime状態は保存データから復元しない
	component.runtimePlaying = false;
	component.runtimeClip = {};
	component.runtimeKey.clear();
	component.runtimeVoiceID = 0;
	component.runtimePlayOnAwakeConsumed = false;
}

void Engine::to_json(nlohmann::json& out, const AudioSourceComponent& component) {

	out["clip"] = ToString(component.clip);
	out["enabled"] = component.enabled;
	out["playOnAwake"] = component.playOnAwake;
	out["loop"] = component.loop;
	out["volume"] = component.volume;
}
