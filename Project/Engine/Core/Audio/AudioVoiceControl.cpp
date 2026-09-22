#include "AudioSystem.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>

void Audio::Stop(const std::string& name) {

	std::lock_guard<std::mutex> lock(mutex_);
	const std::string key = AudioSoundCache::NormalizeKey(name);

	auto it = activeVoices_.find(key);
	if (it == activeVoices_.end()) {
		return;
	}

	for (auto& inst : it->second) {
		if (!inst.voice) {
			continue;
		}

		inst.voice->Stop(0, XAUDIO2_COMMIT_NOW);
		inst.voice->FlushSourceBuffers();
		inst.voice->DestroyVoice();
		inst.voice = nullptr;
	}
	it->second.clear();
	activeVoices_.erase(it);
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
			inst.voice->DestroyVoice();
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
			if (inst.voiceID == voiceID && inst.voice && !inst.paused) {
				// 再生位置を保持したまま停止する、buffer flush / destroyはしない
				inst.voice->Stop(0, XAUDIO2_COMMIT_NOW);
				inst.paused = true;
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
			if (inst.voiceID == voiceID && inst.voice && inst.paused) {
				inst.voice->Start(0, XAUDIO2_COMMIT_NOW);
				inst.paused = false;
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
			AudioSoundData* sound = soundCache_.Find(key);
			if (!sound) {
				return;
			}
			RebuildVoiceBufferLocked(*sound, inst, loop);
			return;
		}
	}
}

void Audio::SetVolume(const std::string& name, float volume) {

	std::lock_guard<std::mutex> lock(mutex_);
	const std::string key = AudioSoundCache::NormalizeKey(name);

	AudioSoundData* sound = soundCache_.Find(key);
	if (!sound) {
		return;
	}

	sound->volume = std::clamp(volume, 0.0f, 1.0f);

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
				return !inst.paused;
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
				inst.voice->DestroyVoice();
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

	AudioSoundData* sound = soundCache_.Find(key);
	if (!sound) {
		return;
	}

	const float mv = std::clamp(masterVolume_, 0.0f, 1.0f);
	const float sv = std::clamp(sound->volume, 0.0f, 1.0f);
	const float iv = std::clamp(inst.instanceVolume, 0.0f, 1.0f);

	const float finalVol = mv * sv * iv;
	inst.voice->SetVolume(finalVol, XAUDIO2_COMMIT_NOW);
}

void Audio::RebuildVoiceBufferLocked(const AudioSoundData& sound, VoiceInstance& inst, bool loop) {

	const WAVEFORMATEX* format = sound.GetFormat();
	if (!inst.voice || !format || format->nBlockAlign == 0) {
		return;
	}

	XAUDIO2_VOICE_STATE state{};
	inst.voice->GetState(&state);
	const uint32_t sampleCount = sound.GetPCMBytes() / format->nBlockAlign;
	if (sampleCount == 0) {
		return;
	}
	const uint32_t currentSample = static_cast<uint32_t>(state.SamplesPlayed % sampleCount);

	inst.voice->Stop(0, XAUDIO2_COMMIT_NOW);
	inst.voice->FlushSourceBuffers();

	XAUDIO2_BUFFER remaining{};
	remaining.pAudioData = sound.GetPCM();
	remaining.AudioBytes = sound.GetPCMBytes();
	remaining.PlayBegin = currentSample;
	remaining.PlayLength = sampleCount - currentSample;
	remaining.Flags = loop ? 0 : XAUDIO2_END_OF_STREAM;
	HRESULT result = inst.voice->SubmitSourceBuffer(&remaining);
	Assert::Call(SUCCEEDED(result), "XAudio2へ再開位置の音声バッファを送信できませんでした");

	if (loop) {
		XAUDIO2_BUFFER loopBuffer{};
		loopBuffer.pAudioData = sound.GetPCM();
		loopBuffer.AudioBytes = sound.GetPCMBytes();
		loopBuffer.LoopCount = XAUDIO2_LOOP_INFINITE;
		loopBuffer.Flags = XAUDIO2_END_OF_STREAM;
		result = inst.voice->SubmitSourceBuffer(&loopBuffer);
		Assert::Call(SUCCEEDED(result), "XAudio2へループ音声バッファを送信できませんでした");
	}

	inst.loop = loop;
	if (!inst.paused) {
		result = inst.voice->Start(0, XAUDIO2_COMMIT_NOW);
		Assert::Call(SUCCEEDED(result), "XAudio2の音声再開に失敗しました");
	}
}
