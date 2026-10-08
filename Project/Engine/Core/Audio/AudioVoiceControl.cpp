#include "AudioSystem.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <cmath>

void Audio::SetMasterVolume(float volume) {

	std::lock_guard<std::mutex> lock(mutex_);
	masterVolume_ = std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 1.0f;
	// 再生中のVoiceにも直ちに反映する
	for (auto& [key, voices] : activeVoices_) {
		for (auto& voice : voices) {
			ApplyVoiceVolumeLocked(key, voice);
		}
	}
}

void Audio::SetGamePauseReason(AudioPauseReason reason, bool paused) {

	std::lock_guard<std::mutex> lock(mutex_);
	if (gamePlaybackState_.HasPauseReason(reason) == paused) { return; }
	gamePlaybackState_.SetPauseReason(reason, paused);
	// GameのVoiceだけを停止し、元から停止中のVoiceは再開しない
	for (auto& [key, voices] : activeVoices_) {
		for (auto& inst : voices) {
			if (inst.owner != AudioPlaybackOwner::Game || !inst.voice ||
				!inst.playbackState.SetPauseReason(reason, paused)) { continue; }
			if (inst.playbackState.IsPaused()) {
				inst.voice->Stop(0, XAUDIO2_COMMIT_NOW);
			} else {
				inst.voice->Start(0, XAUDIO2_COMMIT_NOW);
			}
		}
	}
}

void Audio::StopGameVoices() {

	std::lock_guard<std::mutex> lock(mutex_);
	// Worldと一緒にゲームUIのOneShotも終了する
	for (auto it = activeVoices_.begin(); it != activeVoices_.end();) {
		// 要素移動でPCMが解放される前にVoiceを破棄する
		for (auto& inst : it->second) {
			if (inst.owner == AudioPlaybackOwner::Game) { inst.voice.reset(); }
		}
		std::erase_if(it->second, [](const VoiceInstance& voice) {
			return voice.owner == AudioPlaybackOwner::Game;
		});
		if (it->second.empty()) {
			it = activeVoices_.erase(it);
		} else {
			++it;
		}
	}
}

void Audio::Stop(const std::string& name, AudioPlaybackOwner owner) {

	std::lock_guard<std::mutex> lock(mutex_);
	const std::string key = AudioSoundCache::NormalizeKey(name);

	auto it = activeVoices_.find(key);
	if (it == activeVoices_.end()) {
		return;
	}

	for (auto& inst : it->second) {
		if (!inst.voice || inst.owner != owner) {
			continue;
		}

		inst.voice->Stop(0, XAUDIO2_COMMIT_NOW);
		inst.voice->FlushSourceBuffers();
		inst.voice = nullptr;
	}
	std::erase_if(it->second, [](const VoiceInstance& voice) { return !voice.voice; });
	if (it->second.empty()) { activeVoices_.erase(it); }
}

void Audio::StopVoice(uint64_t voiceID) {

	if (voiceID == 0) {
		return;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	for (auto it = activeVoices_.begin(); it != activeVoices_.end();) {

		auto& voices = it->second;
		for (auto& inst : voices) {
			if (inst.voiceID != voiceID || !inst.voice) {
				continue;
			}

			inst.voice->Stop(0, XAUDIO2_COMMIT_NOW);
			inst.voice->FlushSourceBuffers();
			inst.voice = nullptr;
		}

		voices.erase(std::remove_if(voices.begin(), voices.end(), [](const VoiceInstance& inst) {
			return inst.voice == nullptr;
		}), voices.end());
		if (voices.empty()) {
			it = activeVoices_.erase(it);
		} else {
			++it;
		}
	}
}

void Audio::PauseVoice(uint64_t voiceID) {

	if (voiceID == 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& [key, voices] : activeVoices_) {
		for (auto& inst : voices) {
			if (inst.voiceID == voiceID && inst.voice && inst.playbackState.SetPauseReason(AudioPauseReason::Script, true)) {
				// 再生位置を保持したまま停止する、buffer flush / destroyはしない
				inst.voice->Stop(0, XAUDIO2_COMMIT_NOW);
			}
		}
	}
}

void Audio::ResumeVoice(uint64_t voiceID) {

	if (voiceID == 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& [key, voices] : activeVoices_) {
		for (auto& inst : voices) {
			if (inst.voiceID == voiceID && inst.voice && inst.playbackState.SetPauseReason(AudioPauseReason::Script, false)) {
				inst.voice->Start(0, XAUDIO2_COMMIT_NOW);
			}
		}
	}
}

void Audio::SetVoiceVolume(uint64_t voiceID, float volume) {

	if (voiceID == 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& [key, voices] : activeVoices_) {
		for (auto& inst : voices) {
			if (inst.voiceID != voiceID || !inst.voice) {
				continue;
			}
			inst.instanceVolume = std::clamp(volume, 0.0f, 1.0f);
			ApplyVoiceVolumeLocked(key, inst);
			return;
		}
	}
}

void Audio::SetVoiceLoop(uint64_t voiceID, bool loop) {

	if (voiceID == 0) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& [key, voices] : activeVoices_) {
		for (auto& inst : voices) {
			if (inst.voiceID != voiceID || !inst.voice || inst.loop == loop) {
				continue;
			}
			// 再生開始時のPCMで再生位置を維持する
			RebuildVoiceBufferLocked(*inst.sound, inst, loop);
			return;
		}
	}
}

void Audio::SetVolume(const std::string& name, float volume) {

	std::lock_guard<std::mutex> lock(mutex_);
	const std::string key = AudioSoundCache::NormalizeKey(name);

	const AudioSoundData* sound = soundCache_.Find(key);
	if (!sound) {
		return;
	}

	soundCache_.SetVolume(key, volume);

	// 再生中の全インスタンスにも反映
	CleanupFinishedVoicesLocked(key);

	auto it = activeVoices_.find(key);
	if (it == activeVoices_.end()) {
		return;
	}
	for (auto& inst : it->second) {
		ApplyVoiceVolumeLocked(key, inst);
	}
}

bool Audio::IsPlaying(const std::string& name) {

	std::lock_guard<std::mutex> lock(mutex_);
	const std::string key = AudioSoundCache::NormalizeKey(name);

	auto it = activeVoices_.find(key);
	if (it == activeVoices_.end()) {
		return false;
	}

	CleanupFinishedVoicesLocked(key);

	// まだインスタンスが残っていれば再生中とみなす
	it = activeVoices_.find(key);
	return (it != activeVoices_.end() && !it->second.empty());
}

bool Audio::IsVoicePlaying(uint64_t voiceID) {

	if (voiceID == 0) {
		return false;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	CleanupAllFinishedVoicesLocked();
	for (const auto& [key, voices] : activeVoices_) {
		for (const auto& inst : voices) {
			if (inst.voiceID == voiceID && inst.voice) {
				return !inst.playbackState.IsPaused();
			}
		}
	}
	return false;
}

bool Audio::IsVoiceAlive(uint64_t voiceID) {

	if (voiceID == 0) {
		return false;
	}

	std::lock_guard<std::mutex> lock(mutex_);
	for (const auto& [key, voices] : activeVoices_) {
		for (const auto& inst : voices) {
			if (inst.voiceID == voiceID && inst.voice) {
				return true;
			}
		}
	}
	return false;
}

void Audio::CleanupFinishedVoices() {

	std::lock_guard<std::mutex> lock(mutex_);
	CleanupAllFinishedVoicesLocked();
}

uint64_t Audio::GetVoiceSamplePosition(uint64_t voiceID) {

	std::lock_guard<std::mutex> lock(mutex_);
	for (const auto& [key, voices] : activeVoices_) {
		for (const auto& inst : voices) {
			if (inst.voiceID != voiceID || !inst.voice) { continue; }
			XAUDIO2_VOICE_STATE state{};
			inst.voice->GetState(&state);
			return state.SamplesPlayed;
		}
	}
	return 0;
}

void Audio::CleanupFinishedVoicesLocked(const std::string& key) {

	auto it = activeVoices_.find(key);
	if (it == activeVoices_.end()) {
		return;
	}

	auto& vec = it->second;

	vec.erase(std::remove_if(vec.begin(), vec.end(), [](VoiceInstance& inst) {
			if (!inst.voice) return true;

			XAUDIO2_VOICE_STATE st{};
			inst.voice->GetState(&st);

			// BuffersQueued == 0なら再生完了
			if (st.BuffersQueued == 0) {
				inst.voice = nullptr;
				return true;
			}
			return false;
		}),
		vec.end() );

	if (vec.empty()) {
		activeVoices_.erase(it);
	}
}

void Audio::CleanupAllFinishedVoicesLocked() {

	std::vector<std::string> keys;
	keys.reserve(activeVoices_.size());
	for (auto& [k, _] : activeVoices_) {
		keys.push_back(k);
	}

	for (auto& k : keys) {
		CleanupFinishedVoicesLocked(k);
	}
}

void Audio::ApplyVoiceVolumeLocked(const std::string& key, VoiceInstance& inst) {

	if (!inst.voice) {
		return;
	}

	const AudioSoundData* sound = soundCache_.Find(key);
	if (!sound) {
		return;
	}

	const float mv = std::clamp(masterVolume_, 0.0f, 1.0f);
	const float sv = soundCache_.GetVolume(key);
	const float iv = std::clamp(inst.instanceVolume, 0.0f, 1.0f);

	const float finalVol = mv * sv * iv;
	inst.voice->SetVolume(finalVol, XAUDIO2_COMMIT_NOW);
}
