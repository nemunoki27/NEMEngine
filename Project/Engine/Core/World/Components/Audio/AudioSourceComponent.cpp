#include "AudioSourceComponent.h"

//============================================================================
//	AudioSourceComponent classMethods
//============================================================================
void Engine::AudioSourceComponent::Play() {

	if (!clip) {
		return;
	}
	runtimeCommands.push_back({ AudioSourceCommandType::Play, clip, 1.0f, loop });
	runtimePlaying = true;
	runtimePaused = false;
}

void Engine::AudioSourceComponent::PlayOneShot(AssetID audioClip, float volumeScale) {

	if (!audioClip) {
		return;
	}
	runtimeCommands.push_back({ AudioSourceCommandType::PlayOneShot, audioClip, volumeScale, false });
	runtimePlaying = true;
}

void Engine::AudioSourceComponent::Pause() {

	runtimeCommands.push_back({ AudioSourceCommandType::Pause });
	runtimePaused = runtimePlaying || !runtimePlaybacks.empty();
	runtimePlaying = false;
}

void Engine::AudioSourceComponent::UnPause() {

	runtimeCommands.push_back({ AudioSourceCommandType::UnPause });
	if (runtimePaused) {
		runtimePlaying = true;
	}
	runtimePaused = false;
}

void Engine::AudioSourceComponent::Stop() {

	runtimeCommands.push_back({ AudioSourceCommandType::Stop });
	runtimePlaying = false;
	runtimePaused = false;
}

void Engine::from_json(const nlohmann::json& in, AudioSourceComponent& component) {

	component.clip = ParseAssetID(in, "clip");
	component.enabled = in.value("enabled", component.enabled);
	component.playOnAwake = in.value("playOnAwake", component.playOnAwake);
	component.loop = in.value("loop", component.loop);
	component.volume = in.value("volume", component.volume);

	// Runtime状態は保存データから復元しない
	component.runtimePlaying = false;
	component.runtimePaused = false;
	component.runtimeActive = false;
	component.runtimePlayOnAwakeConsumed = false;
	component.runtimePlaybacks.clear();
	component.runtimeCommands.clear();
}

void Engine::to_json(nlohmann::json& out, const AudioSourceComponent& component) {

	out["clip"] = ToAssetReferenceJson(component.clip);
	out["enabled"] = component.enabled;
	out["playOnAwake"] = component.playOnAwake;
	out["loop"] = component.loop;
	out["volume"] = component.volume;
}
