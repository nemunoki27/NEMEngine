#include "AudioSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <cmath>

bool Engine::Audio::SubmitVoiceBuffersLocked(VoiceInstance& inst, uint32_t position) {

	const auto& sound = *inst.sound;
	uint32_t samples = sound.GetPCMBytes() / sound.GetFormat()->nBlockAlign;
	if (samples == 0) { return false; }
	position %= samples;
	const BYTE* bytes = inst.cursor.reverse ? inst.reversePCM.data() : sound.GetPCM();
	uint32_t begin = inst.cursor.reverse ? samples - 1 - position : position;
	XAUDIO2_VOICE_STATE state{};
	inst.voice->GetState(&state);
	inst.cursor.clockOrigin = state.SamplesPlayed;
	inst.cursor.sampleOrigin = position;
	// 開始位置から終端まで再生し、必要なら全体をループする
	XAUDIO2_BUFFER remaining{};
	remaining.pAudioData = bytes;
	remaining.AudioBytes = sound.GetPCMBytes();
	remaining.PlayBegin = begin;
	remaining.PlayLength = samples - begin;
	remaining.Flags = inst.loop ? 0 : XAUDIO2_END_OF_STREAM;
	HRESULT result = inst.voice->SubmitSourceBuffer(&remaining);
	if (SUCCEEDED(result) && inst.loop) {
		XAUDIO2_BUFFER loop{};
		loop.pAudioData = bytes;
		loop.AudioBytes = sound.GetPCMBytes();
		loop.LoopCount = XAUDIO2_LOOP_INFINITE;
		loop.Flags = XAUDIO2_END_OF_STREAM;
		result = inst.voice->SubmitSourceBuffer(&loop);
	}
	if (FAILED(result)) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "Audio: PCMを送信できません HRESULT={}", result);
		inst.voice.reset();
		return false;
	}
	return true;
}

void Engine::Audio::RebuildVoiceBufferLocked(const AudioSoundData& sound, VoiceInstance& inst, bool loop) {

	XAUDIO2_VOICE_STATE state{};
	inst.voice->GetState(&state);
	uint32_t position = inst.cursor.Resolve(state.SamplesPlayed, sound.GetPCMBytes() / sound.GetFormat()->nBlockAlign);
	inst.voice->Stop(0, XAUDIO2_COMMIT_NOW);
	inst.voice->FlushSourceBuffers();
	inst.loop = loop;
	// 再構築前のClip位置を新しいBufferへ引き継ぐ
	if (SubmitVoiceBuffersLocked(inst, position) && !inst.playbackState.IsPaused()) {
		HRESULT result = inst.voice->Start(0, XAUDIO2_COMMIT_NOW);
		if (FAILED(result)) {
			Logger::Output(LogType::Engine, spdlog::level::warn, "Audio: 再生を再開できません HRESULT={}", result);
			inst.voice.reset();
		}
	}
}

void Engine::Audio::SetVoicePitch(uint64_t voiceID, float pitch) {

	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& [key, voices] : activeVoices_) {
		for (auto& inst : voices) {
			if (inst.voiceID == voiceID && inst.voice) { ApplyVoicePitchLocked(inst, pitch); return; }
		}
	}
}

void Engine::Audio::ApplyVoicePitchLocked(VoiceInstance& inst, float pitch) {

	pitch = std::isfinite(pitch) ? std::clamp(pitch, -3.0f, 3.0f) : 1.0f;
	if (inst.pitch == pitch) { return; }
	bool reverse = pitch == 0.0f ? inst.cursor.reverse : pitch < 0.0f;
	if (reverse && inst.reversePCM.empty()) {
		// 反転PCMの作成が済むまで現在の再生を維持する
		inst.reversePCM = ReverseAudioPCM(*inst.sound);
	}
	HRESULT result = inst.voice->SetFrequencyRatio(std::max(std::abs(pitch), XAUDIO2_MIN_FREQ_RATIO), XAUDIO2_COMMIT_NOW);
	if (FAILED(result)) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "Audio: 再生速度を変更できません HRESULT={}", result);
		return;
	}
	bool changedPause = inst.playbackState.SetPauseReason(AudioPauseReason::ZeroPitch, pitch == 0.0f);
	inst.pitch = pitch;
	if (reverse != inst.cursor.reverse) {
		XAUDIO2_VOICE_STATE state{};
		inst.voice->GetState(&state);
		uint32_t count = inst.sound->GetPCMBytes() / inst.sound->GetFormat()->nBlockAlign;
		uint32_t position = inst.cursor.Resolve(state.SamplesPlayed, count);
		inst.voice->Stop(0, XAUDIO2_COMMIT_NOW);
		inst.voice->FlushSourceBuffers();
		inst.cursor.reverse = reverse;
		if (!SubmitVoiceBuffersLocked(inst, position)) { return; }
		changedPause = true;
	}
	if (changedPause) {
		if (inst.playbackState.IsPaused()) { inst.voice->Stop(0, XAUDIO2_COMMIT_NOW); }
		else { inst.voice->Start(0, XAUDIO2_COMMIT_NOW); }
	}
}
