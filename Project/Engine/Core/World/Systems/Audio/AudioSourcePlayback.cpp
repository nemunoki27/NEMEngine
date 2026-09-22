#include "AudioSourcePlayback.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>

namespace {

	// AudioSourceComponentの再生キーを作成する
	std::string BuildAudioKey(const std::filesystem::path& fullPath) {

		return fullPath.empty() ? std::string{} : Engine::Algorithm::PathToUTF8(fullPath.stem());
	}
}

void Engine::AudioSourcePlayback::StopPlayback(AudioSourcePlaybackRuntime& playback) {

	if (playback.voiceID != 0) {
		Audio::GetInstance()->StopVoice(playback.voiceID);
		runtimeVoices_.erase(playback.voiceID);
	}
	playback.voiceID = 0;
}

void Engine::AudioSourcePlayback::StopSourceVoices( AudioSourceRuntimeData& runtime) {

	for (AudioSourcePlaybackRuntime& playback : runtime.playbacks) {
		StopPlayback(playback);
	}
	runtime.playbacks.clear();
	runtime.playing = false;
	runtime.paused = false;
}

bool Engine::AudioSourcePlayback::StartPlayback( Entity entity, const AudioSourceComponent& component,
	AudioSourceRuntimeData& runtime,
	AssetID clip, bool primary, bool loop, float volumeScale, AssetDatabase& database) {

	const std::filesystem::path fullPath = database.ResolveFullPath(clip);
	if (fullPath.empty() || !Audio::GetInstance()->EnsureLoaded(fullPath)) {
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
	runtime.playbacks.push_back(std::move(playback));
	return true;
}

void Engine::AudioSourcePlayback::ProcessCommands(Entity entity, const AudioSourceComponent& component,
	AudioSourceRuntimeData& runtime, AssetDatabase& database) {

	std::vector<AudioSourceCommand> commands =
		std::move(runtime.commands);
	runtime.commands.clear();
	Audio* audio = Audio::GetInstance();

	for (const AudioSourceCommand& command : commands) {
		switch (command.type) {
		case AudioSourceCommandType::Play:
			for (auto it = runtime.playbacks.begin();
				it != runtime.playbacks.end();) {
				if (!it->primary) {
					++it;
					continue;
				}
				StopPlayback(*it);
				it = runtime.playbacks.erase(it);
			}
			StartPlayback(entity, component, runtime, command.clip,
				true, command.loop, 1.0f, database);
			break;
		case AudioSourceCommandType::PlayOneShot:
			StartPlayback(entity, component, runtime, command.clip,
				false, false, command.volumeScale, database);
			break;
		case AudioSourceCommandType::Pause:
			for (AudioSourcePlaybackRuntime& playback : runtime.playbacks) {
				if (!playback.paused) {
					audio->PauseVoice(playback.voiceID);
					playback.paused = true;
				}
			}
			break;
		case AudioSourceCommandType::UnPause:
			for (AudioSourcePlaybackRuntime& playback : runtime.playbacks) {
				if (playback.paused) {
					audio->ResumeVoice(playback.voiceID);
					playback.paused = false;
				}
			}
			break;
		case AudioSourceCommandType::Stop:
			StopSourceVoices(runtime);
			break;
		}
	}
}

void Engine::AudioSourcePlayback::UpdatePlaybackSettings(
	const AudioSourceComponent& component, AudioSourceRuntimeData& runtime) {

	Audio* audio = Audio::GetInstance();
	const float sourceVolume = std::clamp(component.volume, 0.0f, 1.0f);
	for (AudioSourcePlaybackRuntime& playback : runtime.playbacks) {
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

void Engine::AudioSourcePlayback::CleanupFinishedPlaybacks( AudioSourceRuntimeData& runtime) {

	Audio* audio = Audio::GetInstance();
	runtime.playbacks.erase(std::remove_if( runtime.playbacks.begin(), runtime.playbacks.end(),
		[&](const AudioSourcePlaybackRuntime& playback) {
			if (audio->IsVoiceAlive(playback.voiceID)) {
				return false;
			}
			runtimeVoices_.erase(playback.voiceID);
			return true;
		}), runtime.playbacks.end());
}

void Engine::AudioSourcePlayback::RefreshRuntimeState( AudioSourceRuntimeData& runtime) {

	runtime.playing = std::any_of(
		runtime.playbacks.begin(), runtime.playbacks.end(), [](const AudioSourcePlaybackRuntime& playback) { return !playback.paused; });
	runtime.paused = !runtime.playbacks.empty() && !runtime.playing;
}

void Engine::AudioSourcePlayback::StopOrphanVoices(ECSWorld& world) {

	Audio* audio = Audio::GetInstance();
	for (auto it = runtimeVoices_.begin(); it != runtimeVoices_.end();) {

		const uint64_t voiceID = it->first;
		const Entity entity = it->second;
		const AudioSourceRuntimeData* runtime = world.IsAlive(entity) ?
			TryGetAudioSourceRuntime(world, entity) : nullptr;
		const bool owned = runtime && std::any_of(
			runtime->playbacks.begin(), runtime->playbacks.end(),
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

void Engine::AudioSourcePlayback::StopAll(ECSWorld& world) {

	world.ForEach<AudioSourceComponent, AudioSourceRuntimeComponent>(
		[&](Entity entity, AudioSourceComponent&,
			[[maybe_unused]] AudioSourceRuntimeComponent& runtimeComponent) {

		AudioSourceRuntimeData* runtime =
			TryGetAudioSourceRuntime(world, entity);
		if (!runtime) {
			return;
		}
		StopSourceVoices(*runtime);
		runtime->active = false;
		runtime->playOnAwakeConsumed = false;
		runtime->commands.clear();
		});

	Audio* audio = Audio::GetInstance();
	for (const auto& voice : runtimeVoices_) {

		audio->StopVoice(voice.first);
	}
	runtimeVoices_.clear();
}
