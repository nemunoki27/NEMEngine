#include "AudioSourceSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Audio/Audio.h>
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Core/ECS/Component/Builtin/AudioSourceComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>

// c++
#include <filesystem>
#include <string>

//============================================================================
//	AudioSourceSystem classMethods
//============================================================================

namespace {

	// EntityがHierarchy上で有効かを返す
	bool IsEntityActiveInHierarchy(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity)) {
			return false;
		}
		if (!world.HasComponent<Engine::SceneObjectComponent>(entity)) {
			return true;
		}
		return world.GetComponent<Engine::SceneObjectComponent>(entity).activeInHierarchy;
	}

	// AudioSourceComponentの再生キーを作成する
	std::string BuildAudioKey(const Engine::AssetDatabase& database, Engine::AssetID clip) {

		const std::filesystem::path fullPath = database.ResolveFullPath(clip);
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

		// ワンショット再生が自然終了したらRuntime状態を戻す
		if (component.runtimePlaying && !component.loop && !audio->IsVoicePlaying(component.runtimeVoiceID)) {

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

		component.runtimeKey = BuildAudioKey(database, component.clip);
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
