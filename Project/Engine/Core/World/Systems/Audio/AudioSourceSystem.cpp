#include "AudioSourceSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

// c++
#include <filesystem>
#include <string>

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

	Audio* audio = Audio::GetInstance();
	AssetDatabase& database = *context.assetDatabase;

	world.ForEach<AudioSourceComponent>([&](Entity entity, AudioSourceComponent& component) {

		// gameplay(C#)からの明示Play/Pause/Stop要求を先に消費する
		if (component.runtimePlayRequest != 0) {

			const int request = component.runtimePlayRequest;
			component.runtimePlayRequest = 0;
			if (request == 3) {
				// Stop: voice破棄
				if (component.runtimeVoiceID != 0) {
					audio->StopVoice(component.runtimeVoiceID);
				}
				component.runtimePlaying = false;
				component.runtimeClip = {};
				component.runtimeKey.clear();
				component.runtimeVoiceID = 0;
				component.runtimePaused = false;
			} else if (request == 2) {
				// Pause:再生位置を保持して停止
				if (component.runtimePlaying && component.runtimeVoiceID != 0 && !component.runtimePaused) {
					audio->PauseVoice(component.runtimeVoiceID);
					component.runtimePaused = true;
				}
			} else if (request == 1) {
				// Play: pause中ならresume、未再生なら明示再生する
				if (component.runtimePaused && component.runtimeVoiceID != 0) {
					audio->ResumeVoice(component.runtimeVoiceID);
					component.runtimePaused = false;
				} else if (!component.runtimePlaying && component.clip) {
					const std::filesystem::path fullPath = database.ResolveFullPath(component.clip);
					if (!fullPath.empty() && audio->EnsureLoaded(fullPath.string())) {
						component.runtimeKey = BuildAudioKey(fullPath);
						component.runtimeClip = component.clip;
						component.runtimeVoiceID = audio->PlayManaged(component.runtimeKey, component.loop, component.volume);
						component.runtimePlaying = component.runtimeVoiceID != 0;
						component.runtimePaused = false;
						component.runtimePlayOnAwakeConsumed = true;
					}
				}
			}
		}

		const bool canPlay =
			component.enabled &&
			component.clip &&
			IsEntityActiveInHierarchy(world, entity);

		// 無効化、非アクティブ化、Clip変更時は前回の再生を止める
		if (component.runtimePlaying && (!canPlay || component.runtimeClip != component.clip)) {

			const bool clipChanged = component.runtimeClip != component.clip;
			if (component.runtimeVoiceID != 0) {
				audio->StopVoice(component.runtimeVoiceID);
			}
			component.runtimePlaying = false;
			component.runtimeClip = {};
			component.runtimeKey.clear();
			component.runtimeVoiceID = 0;
			if (clipChanged) {
				component.runtimePlayOnAwakeConsumed = false;
			}
		}

		// ワンショット再生が自然終了したらRuntime状態を戻す、pause中はvoiceが止まっていても終了扱いにしない
		if (component.runtimePlaying && !component.runtimePaused && !component.loop && !audio->IsVoicePlaying(component.runtimeVoiceID)) {

			component.runtimePlaying = false;
			component.runtimeClip = {};
			component.runtimeKey.clear();
			component.runtimeVoiceID = 0;
		}

		if (!canPlay || component.runtimePlaying || !component.playOnAwake || component.runtimePlayOnAwakeConsumed) {
			return;
		}

		const std::filesystem::path fullPath = database.ResolveFullPath(component.clip);
		if (fullPath.empty()) {
			return;
		}

		const std::string fullPathString = fullPath.string();
		if (!audio->EnsureLoaded(fullPathString)) {
			return;
		}

		component.runtimeKey = BuildAudioKey(fullPath);
		component.runtimeClip = component.clip;
		component.runtimePlaying = true;

		component.runtimeVoiceID = audio->PlayManaged(component.runtimeKey, component.loop, component.volume);
		if (component.runtimeVoiceID == 0) {

			component.runtimePlaying = false;
			component.runtimeClip = {};
			component.runtimeKey.clear();
			component.runtimePlayOnAwakeConsumed = false;
		} else {

			component.runtimePlayOnAwakeConsumed = true;
		}
		});
}

void Engine::AudioSourceSystem::StopAll(ECSWorld& world) {

	Audio* audio = Audio::GetInstance();
	world.ForEach<AudioSourceComponent>([&](Entity, AudioSourceComponent& component) {

		if (component.runtimePlaying && component.runtimeVoiceID != 0) {
			audio->StopVoice(component.runtimeVoiceID);
		}
		component.runtimePlaying = false;
		component.runtimeClip = {};
		component.runtimeKey.clear();
		component.runtimeVoiceID = 0;
		component.runtimePlayOnAwakeConsumed = false;
		});
}
