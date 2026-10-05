#include "AudioPreviewSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

Engine::AudioPreviewSession::~AudioPreviewSession() {

	Stop();
}

bool Engine::AudioPreviewSession::Play(const std::filesystem::path& path, bool loop, float volume, float pitch) {

	Stop();
	Audio* audio = Audio::GetInstance();
	if (!audio->EnsureLoaded(path)) { return false; }
	voiceID_ = audio->PlayManaged(Algorithm::PathToUTF8(path), loop, volume, AudioPlaybackOwner::EditorPreview, pitch);
	return voiceID_ != 0;
}

void Engine::AudioPreviewSession::Stop() {

	// Audio終了後に新しいDeviceを作らない
	if (voiceID_ != 0) {
		if (Audio* audio = Audio::TryGetInstance()) { audio->StopVoice(voiceID_); }
		voiceID_ = 0;
	}
}
