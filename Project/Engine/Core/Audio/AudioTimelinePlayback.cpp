#include "AudioSystem.h"

//============================================================================
//	include
//============================================================================
// c++
#include <cmath>

uint64_t Engine::Audio::PlayManagedAt(const std::string& name, bool loop, double seconds,
	float volume, AudioPlaybackOwner owner, float pitch) {

	return PlayInternal(name, loop, volume, owner, pitch, seconds);
}

bool Engine::Audio::SeekVoice(uint64_t voiceID, double seconds) {

	if (!std::isfinite(seconds) || seconds < 0) return false;
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& [key, voices] : activeVoices_) {
		for (auto& instance : voices) {
			if (instance.voiceID != voiceID || !instance.voice) continue;
			instance.voice->Stop(0, XAUDIO2_COMMIT_NOW);
			instance.voice->FlushSourceBuffers();
			if (!SubmitVoiceBuffersLocked(instance, ResolveAudioSamplePosition(*instance.sound, seconds, instance.loop))) return false;
			if (!instance.playbackState.IsPaused()) return SUCCEEDED(instance.voice->Start(0, XAUDIO2_COMMIT_NOW));
			return true;
		}
	}
	return false;
}

double Engine::Audio::GetVoicePlaybackTime(uint64_t voiceID) {

	std::lock_guard<std::mutex> lock(mutex_);
	for (const auto& [key, voices] : activeVoices_) {
		for (const auto& instance : voices) {
			if (instance.voiceID != voiceID || !instance.voice) continue;
			const auto* format = instance.sound->GetFormat();
			if (!format->nBlockAlign || !format->nSamplesPerSec) return 0;
			XAUDIO2_VOICE_STATE state{};
			instance.voice->GetState(&state);
			return static_cast<double>(instance.cursor.Resolve(state.SamplesPlayed,
				instance.sound->GetPCMBytes() / format->nBlockAlign)) / format->nSamplesPerSec;
		}
	}
	return 0;
}

double Engine::Audio::GetSoundDuration(const std::string& name) {

	std::lock_guard<std::mutex> lock(mutex_);
	const auto* sound = soundCache_.Find(AudioSoundCache::NormalizeKey(name));
	if (!sound || !sound->GetFormat()->nSamplesPerSec || !sound->GetFormat()->nBlockAlign) return 0;
	return static_cast<double>(sound->GetPCMBytes()) / sound->GetFormat()->nBlockAlign / sound->GetFormat()->nSamplesPerSec;
}
