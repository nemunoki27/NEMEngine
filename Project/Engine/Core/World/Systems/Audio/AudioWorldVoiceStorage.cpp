#include "AudioWorldVoiceStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>

// c++
#include <algorithm>

Engine::AudioWorldVoiceStorage::~AudioWorldVoiceStorage() {

	Clear();
}

void Engine::AudioWorldVoiceStorage::Add(Entity entity, uint64_t voiceID) {

	if (voiceID != 0) { voices_.push_back({ entity, voiceID }); }
}

void Engine::AudioWorldVoiceStorage::Update(ECSWorld& world) {

	Audio* audio = Audio::TryGetInstance();
	if (!audio) { voices_.clear(); return; }
	std::erase_if(voices_, [&](const Entry& entry) {
		if (!audio->IsVoiceAlive(entry.voiceID)) { return true; }
		if (world.IsAlive(entry.entity) && !world.IsPendingDestroy(entry.entity) &&
			world.HasComponent<UISelectableComponent>(entry.entity) &&
			IsEntityActiveInHierarchy(world, entry.entity)) { return false; }
		// Sceneの削除時も発音EntityのVoiceを残さない
		audio->StopVoice(entry.voiceID);
		return true;
	});
}

void Engine::AudioWorldVoiceStorage::Clear() {

	if (Audio* audio = Audio::TryGetInstance()) {
		for (const Entry& entry : voices_) { audio->StopVoice(entry.voiceID); }
	}
	voices_.clear();
}
