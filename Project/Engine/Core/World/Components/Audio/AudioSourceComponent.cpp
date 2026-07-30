#include "AudioSourceComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	AudioSourceComponent classMethods
//============================================================================
void Engine::AudioSourceRuntimeComponent::OnAdded(
	ECSWorld& world, [[maybe_unused]] const Entity& entity,
	AudioSourceRuntimeComponent& component) {

	if (!component.handle.IsValid()) {
		component.handle =
			world.GetStorage().Get<AudioSourceRuntimeStorage>().Emplace();
	}
}

void Engine::AudioSourceRuntimeComponent::InitializeStorage(
	ECSWorld& world, const Entity& entity,
	AudioSourceRuntimeComponent& component) {

	OnAdded(world, entity, component);
}

void Engine::AudioSourceRuntimeComponent::ReleaseStorage(
	ECSWorld& world, [[maybe_unused]] const Entity& entity,
	AudioSourceRuntimeComponent& component) {

	if (component.handle.IsValid()) {
		AudioSourceRuntimeStorage& storage =
			world.GetStorage().Get<AudioSourceRuntimeStorage>();
		if (AudioSourceRuntimeData* runtime =
			storage.TryGet(component.handle)) {

			// Component単体削除でも再生Voiceを残さない
			for (const AudioSourcePlaybackRuntime& playback :
				runtime->playbacks) {

				if (playback.voiceID != 0) {
					Audio::GetInstance()->StopVoice(playback.voiceID);
				}
			}
		}
		storage.Release(component.handle);
		component.handle = AudioSourceRuntimeHandle::Null();
	}
}

void Engine::AudioSourceRuntimeComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const nlohmann::json& in,
	[[maybe_unused]] AudioSourceRuntimeComponent& component) {
}

void Engine::AudioSourceRuntimeComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const AudioSourceRuntimeComponent& component,
	nlohmann::json& out) {

	out = nlohmann::json::object();
}

void Engine::AudioSourceComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] AudioSourceComponent& component) {

	if (!world.HasComponent<AudioSourceRuntimeComponent>(entity)) {
		world.AddComponent<AudioSourceRuntimeComponent>(entity);
	}
}

void Engine::AudioSourceComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasComponent<AudioSourceRuntimeComponent>(entity)) {
		world.RemoveComponent<AudioSourceRuntimeComponent>(entity);
	}
}

void Engine::AudioSourceComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] AudioSourceComponent& component) {
}

void Engine::AudioSourceComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] AudioSourceComponent& component) {
}

void Engine::AudioSourceComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const nlohmann::json& in, AudioSourceComponent& component) {

	from_json(in, component);
}

void Engine::AudioSourceComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const AudioSourceComponent& component, nlohmann::json& out) {

	to_json(out, component);
}

Engine::AudioSourceRuntimeData* Engine::TryGetAudioSourceRuntime(
	ECSWorld& world, const Entity& entity) {

	AudioSourceRuntimeComponent* runtime =
		world.TryGetComponent<AudioSourceRuntimeComponent>(entity);
	if (!runtime) {
		return nullptr;
	}
	return world.GetStorage().Get<AudioSourceRuntimeStorage>().TryGet(
		runtime->handle);
}

const Engine::AudioSourceRuntimeData* Engine::TryGetAudioSourceRuntime(
	const ECSWorld& world, const Entity& entity) {

	const AudioSourceRuntimeComponent* runtime =
		world.TryGetComponent<AudioSourceRuntimeComponent>(entity);
	const AudioSourceRuntimeStorage* storage =
		world.GetStorage().TryGet<AudioSourceRuntimeStorage>();
	return runtime && storage ? storage->TryGet(runtime->handle) : nullptr;
}

void Engine::RequestAudioPlay(
	ECSWorld& world, const Entity& entity) {

	const AudioSourceComponent* source =
		world.TryGetComponent<AudioSourceComponent>(entity);
	AudioSourceRuntimeData* runtime =
		TryGetAudioSourceRuntime(world, entity);
	if (!source || !runtime || !source->clip) {
		return;
	}
	runtime->commands.push_back({
		AudioSourceCommandType::Play, source->clip, 1.0f, source->loop });
	runtime->playing = true;
	runtime->paused = false;
}

void Engine::RequestAudioPlayOneShot(
	ECSWorld& world, const Entity& entity,
	AssetID clip, float volumeScale) {

	AudioSourceRuntimeData* runtime =
		TryGetAudioSourceRuntime(world, entity);
	if (!runtime || !clip) {
		return;
	}
	runtime->commands.push_back({
		AudioSourceCommandType::PlayOneShot, clip, volumeScale, false });
	runtime->playing = true;
}

void Engine::RequestAudioPause(
	ECSWorld& world, const Entity& entity) {

	AudioSourceRuntimeData* runtime =
		TryGetAudioSourceRuntime(world, entity);
	if (!runtime) {
		return;
	}
	runtime->commands.push_back({ AudioSourceCommandType::Pause });
	runtime->paused = runtime->playing || !runtime->playbacks.empty();
	runtime->playing = false;
}

void Engine::RequestAudioUnPause(
	ECSWorld& world, const Entity& entity) {

	AudioSourceRuntimeData* runtime =
		TryGetAudioSourceRuntime(world, entity);
	if (!runtime) {
		return;
	}
	runtime->commands.push_back({ AudioSourceCommandType::UnPause });
	if (runtime->paused) {
		runtime->playing = true;
	}
	runtime->paused = false;
}

void Engine::RequestAudioStop(
	ECSWorld& world, const Entity& entity) {

	AudioSourceRuntimeData* runtime =
		TryGetAudioSourceRuntime(world, entity);
	if (!runtime) {
		return;
	}
	runtime->commands.push_back({ AudioSourceCommandType::Stop });
	runtime->playing = false;
	runtime->paused = false;
}

bool Engine::IsAudioSourcePlaying(
	const ECSWorld& world, const Entity& entity) {

	const AudioSourceRuntimeData* runtime =
		TryGetAudioSourceRuntime(world, entity);
	return runtime && runtime->playing;
}

void Engine::from_json(const nlohmann::json& in, AudioSourceComponent& component) {

	component.clip = ParseAssetID(in, "clip");
	component.enabled = in.value("enabled", component.enabled);
	component.playOnAwake = in.value("playOnAwake", component.playOnAwake);
	component.loop = in.value("loop", component.loop);
	component.volume = in.value("volume", component.volume);
}

void Engine::to_json(nlohmann::json& out, const AudioSourceComponent& component) {

	out["clip"] = ToAssetReferenceJson(component.clip);
	out["enabled"] = component.enabled;
	out["playOnAwake"] = component.playOnAwake;
	out["loop"] = component.loop;
	out["volume"] = component.volume;
}
