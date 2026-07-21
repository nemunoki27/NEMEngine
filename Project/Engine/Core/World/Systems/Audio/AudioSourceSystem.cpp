#include "AudioSourceSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>

//============================================================================
//	AudioSourceSystem classMethods
//============================================================================

namespace {

	// AudioSourceComponentの再生キーを作成する
	std::string BuildAudioKey(const std::filesystem::path& fullPath) {

		return fullPath.empty() ? std::string{} : fullPath.stem().string();
	}
}

void Engine::AudioSourceSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	StopAll(world);
}

void Engine::AudioSourceSystem::Update(ECSWorld& world, SystemContext& context) {

	// Play中だけComponentの自動再生を管理する
	if (context.mode != WorldMode::Play || !context.assetDatabase) {
		return;
	}

	AssetDatabase& database = *context.assetDatabase;
	Audio::GetInstance()->CleanupFinishedVoices();

	world.ForEach<AudioSourceComponent>([&](Entity entity, AudioSourceComponent& component) {

		const bool active = component.enabled && IsEntityActiveInHierarchy(world, entity);
		if (!active) {
			StopSourceVoices(component);
			component.runtimeCommands.clear();
			component.runtimeActive = false;
			component.runtimePlayOnAwakeConsumed = false;
			RefreshRuntimeState(component);
			return;
		}

		const bool activated = !component.runtimeActive;
		component.runtimeActive = true;
		if (activated && component.playOnAwake && !component.runtimePlayOnAwakeConsumed) {
			component.runtimePlayOnAwakeConsumed = true;
			if (component.clip) {
				StartPlayback(entity, component, component.clip,
					true, component.loop, 1.0f, database);
			}
		}

		ProcessCommands(entity, component, database);
		CleanupFinishedPlaybacks(component);
		UpdatePlaybackSettings(component);
		RefreshRuntimeState(component);
		});
}

void Engine::AudioSourceSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	if (context.mode != WorldMode::Play) {
		return;
	}
	StopOrphanVoices(world);
}

void Engine::AudioSourceSystem::StopPlayback(AudioSourcePlaybackRuntime& playback) {

	if (playback.voiceID != 0) {
		Audio::GetInstance()->StopVoice(playback.voiceID);
		runtimeVoices_.erase(playback.voiceID);
	}
	playback.voiceID = 0;
}

void Engine::AudioSourceSystem::StopSourceVoices(AudioSourceComponent& component) {

	for (AudioSourcePlaybackRuntime& playback : component.runtimePlaybacks) {
		StopPlayback(playback);
	}
	component.runtimePlaybacks.clear();
	component.runtimePlaying = false;
	component.runtimePaused = false;
}

bool Engine::AudioSourceSystem::StartPlayback(Entity entity, AudioSourceComponent& component,
	AssetID clip, bool primary, bool loop, float volumeScale, AssetDatabase& database) {

	const std::filesystem::path fullPath = database.ResolveFullPath(clip);
	if (fullPath.empty() || !Audio::GetInstance()->EnsureLoaded(fullPath.string())) {
		return false;
	}

	AudioSourcePlaybackRuntime playback{};
	playback.clip = clip;
	playback.key = BuildAudioKey(fullPath);
	playback.volumeScale = std::clamp(volumeScale, 0.0f, 1.0f);
	playback.appliedVolume = std::clamp(component.volume, 0.0f, 1.0f) * playback.volumeScale;
	playback.primary = primary;
	playback.loop = loop;
	playback.voiceID = Audio::GetInstance()->PlayManaged(
		playback.key, loop, playback.appliedVolume);
	if (playback.voiceID == 0) {
		return false;
	}

	runtimeVoices_.insert_or_assign(playback.voiceID, entity);
	component.runtimePlaybacks.push_back(std::move(playback));
	return true;
}

void Engine::AudioSourceSystem::ProcessCommands(Entity entity,
	AudioSourceComponent& component, AssetDatabase& database) {

	std::vector<AudioSourceCommand> commands = std::move(component.runtimeCommands);
	component.runtimeCommands.clear();
	Audio* audio = Audio::GetInstance();

	for (const AudioSourceCommand& command : commands) {
		switch (command.type) {
		case AudioSourceCommandType::Play:
			for (auto it = component.runtimePlaybacks.begin();
				it != component.runtimePlaybacks.end();) {
				if (!it->primary) {
					++it;
					continue;
				}
				StopPlayback(*it);
				it = component.runtimePlaybacks.erase(it);
			}
			StartPlayback(entity, component, command.clip,
				true, command.loop, 1.0f, database);
			break;
		case AudioSourceCommandType::PlayOneShot:
			StartPlayback(entity, component, command.clip,
				false, false, command.volumeScale, database);
			break;
		case AudioSourceCommandType::Pause:
			for (AudioSourcePlaybackRuntime& playback : component.runtimePlaybacks) {
				if (!playback.paused) {
					audio->PauseVoice(playback.voiceID);
					playback.paused = true;
				}
			}
			break;
		case AudioSourceCommandType::UnPause:
			for (AudioSourcePlaybackRuntime& playback : component.runtimePlaybacks) {
				if (playback.paused) {
					audio->ResumeVoice(playback.voiceID);
					playback.paused = false;
				}
			}
			break;
		case AudioSourceCommandType::Stop:
			StopSourceVoices(component);
			break;
		}
	}
}

void Engine::AudioSourceSystem::UpdatePlaybackSettings(AudioSourceComponent& component) {

	Audio* audio = Audio::GetInstance();
	const float sourceVolume = std::clamp(component.volume, 0.0f, 1.0f);
	for (AudioSourcePlaybackRuntime& playback : component.runtimePlaybacks) {
		const float volume = sourceVolume * playback.volumeScale;
		if (std::abs(playback.appliedVolume - volume) > 0.0001f) {
			audio->SetVoiceVolume(playback.voiceID, volume);
			playback.appliedVolume = volume;
		}
		if (playback.primary && playback.loop != component.loop) {
			audio->SetVoiceLoop(playback.voiceID, component.loop);
			playback.loop = component.loop;
		}
	}
}

void Engine::AudioSourceSystem::CleanupFinishedPlaybacks(AudioSourceComponent& component) {

	Audio* audio = Audio::GetInstance();
	component.runtimePlaybacks.erase(std::remove_if(
		component.runtimePlaybacks.begin(), component.runtimePlaybacks.end(),
		[&](const AudioSourcePlaybackRuntime& playback) {
			if (audio->IsVoiceAlive(playback.voiceID)) {
				return false;
			}
			runtimeVoices_.erase(playback.voiceID);
			return true;
		}), component.runtimePlaybacks.end());
}

void Engine::AudioSourceSystem::RefreshRuntimeState(AudioSourceComponent& component) {

	component.runtimePlaying = std::any_of(
		component.runtimePlaybacks.begin(), component.runtimePlaybacks.end(),
		[](const AudioSourcePlaybackRuntime& playback) { return !playback.paused; });
	component.runtimePaused = !component.runtimePlaybacks.empty() && !component.runtimePlaying;
}

void Engine::AudioSourceSystem::StopOrphanVoices(ECSWorld& world) {

	Audio* audio = Audio::GetInstance();
	for (auto it = runtimeVoices_.begin(); it != runtimeVoices_.end();) {

		const uint64_t voiceID = it->first;
		const Entity entity = it->second;
		const AudioSourceComponent* component = world.IsAlive(entity)
			? world.TryGetComponent<AudioSourceComponent>(entity) : nullptr;
		const bool owned = component && std::any_of(
			component->runtimePlaybacks.begin(), component->runtimePlaybacks.end(),
			[voiceID](const AudioSourcePlaybackRuntime& playback) {
				return playback.voiceID == voiceID;
			});
		if (owned) {

			++it;
			continue;
		}

		audio->StopVoice(voiceID);
		it = runtimeVoices_.erase(it);
	}
}

void Engine::AudioSourceSystem::StopAll(ECSWorld& world) {

	world.ForEach<AudioSourceComponent>([&](Entity, AudioSourceComponent& component) {

		StopSourceVoices(component);
		component.runtimeActive = false;
		component.runtimePlayOnAwakeConsumed = false;
		component.runtimeCommands.clear();
		});

	Audio* audio = Audio::GetInstance();
	for (const auto& voice : runtimeVoices_) {

		audio->StopVoice(voice.first);
	}
	runtimeVoices_.clear();
}
