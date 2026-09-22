#include "AudioSourceSystem.h"

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

//============================================================================
//	AudioSourceSystem classMethods
//============================================================================

void Engine::AudioSourceSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	playback_.StopAll(world);
}

void Engine::AudioSourceSystem::Update(ECSWorld& world, SystemContext& context) {

	// Play中だけComponentの自動再生を管理する
	if (context.mode != WorldMode::Play || !context.assetDatabase) {
		return;
	}

	AssetDatabase& database = *context.assetDatabase;
	Audio::GetInstance()->CleanupFinishedVoices();

	world.ForEach<AudioSourceComponent, AudioSourceRuntimeComponent>(
		[&](Entity entity, AudioSourceComponent& component,
			[[maybe_unused]] AudioSourceRuntimeComponent& runtimeComponent) {

		AudioSourceRuntimeData* runtime =
			TryGetAudioSourceRuntime(world, entity);
		if (!runtime) {
			return;
		}

		const bool active = component.enabled && IsEntityActiveInHierarchy(world, entity);
		if (!active) {
			playback_.StopSourceVoices(*runtime);
			runtime->commands.clear();
			runtime->active = false;
			runtime->playOnAwakeConsumed = false;
			playback_.RefreshRuntimeState(*runtime);
			return;
		}

		const bool activated = !runtime->active;
		runtime->active = true;
		if (activated && component.playOnAwake && !runtime->playOnAwakeConsumed) {
			runtime->playOnAwakeConsumed = true;
			if (component.clip) {
				playback_.StartPlayback(entity, component, *runtime, component.clip,
					true, component.loop, 1.0f, database);
			}
		}

		playback_.ProcessCommands(entity, component, *runtime, database);
		playback_.CleanupFinishedPlaybacks(*runtime);
		playback_.UpdatePlaybackSettings(component, *runtime);
		playback_.RefreshRuntimeState(*runtime);
		});
}

void Engine::AudioSourceSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	if (context.mode != WorldMode::Play) {
		return;
	}
	playback_.StopOrphanVoices(world);
}
