#include "AudioSpatialSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/World/Components/Audio/AudioListenerComponent.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

void Engine::AudioSpatialSystem::OnWorldExit([[maybe_unused]] ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	Audio::GetInstance()->SetListener({});
	multipleListeners_ = false;
	missingListener_ = false;
}

void Engine::AudioSpatialSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	if (context.mode != WorldMode::Play) { return; }
	// Transform確定後の位置と向きから単一Listenerを選ぶ
	AudioListenerState listener;
	uint32_t listenerCount = 0;
	world.ForEach<AudioListenerComponent, TransformComponent>(
		[&](Entity entity, AudioListenerComponent& component, TransformComponent& transform) {
			if (!component.enabled || !IsEntityActiveInHierarchy(world, entity)) { return; }
			++listenerCount;
			listener.position = transform.worldMatrix.GetTranslationValue();
			auto& matrix = transform.worldMatrix.m;
			listener.forward = Vector3::NormalizeOr({ matrix[2][0], matrix[2][1], matrix[2][2] }, { 0.0f, 0.0f, 1.0f });
			Vector3 right = Vector3::NormalizeOr(Vector3::Cross(
				{ matrix[1][0], matrix[1][1], matrix[1][2] }, listener.forward), { 1.0f, 0.0f, 0.0f });
			listener.up = Vector3::NormalizeOr(Vector3::Cross(listener.forward, right), { 0.0f, 1.0f, 0.0f });
		});
	listener.active = listenerCount == 1;
	if (listenerCount > 1 && !multipleListeners_) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "有効なAudio Listenerは1つにしてください。3D音声を停止します");
	}
	multipleListeners_ = listenerCount > 1;
	Audio* audio = Audio::GetInstance();
	audio->SetListener(listener);
	bool requiresListener = false;
	// 各SourceのOneShotも同じ位置と減衰設定を使う
	world.ForEach<AudioSourceComponent, TransformComponent>(
		[&](Entity entity, AudioSourceComponent& component, TransformComponent& transform) {
			auto* runtime = TryGetAudioSourceRuntime(world, entity);
			if (!runtime) { return; }
			requiresListener |= component.spatialBlend > 0.0f && !runtime->playbacks.empty();
			AudioSpatialState spatial{ transform.worldMatrix.GetTranslationValue(), component.spatialBlend,
				component.minDistance, component.maxDistance, component.rolloffMode };
			for (const auto& playback : runtime->playbacks) { audio->SetVoiceSpatial(playback.voiceID, spatial); }
		});
	bool missing = listenerCount == 0 && requiresListener;
	if (missing && !missingListener_) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "3D音声には有効なAudio Listenerが必要です");
	}
	missingListener_ = missing;
}
