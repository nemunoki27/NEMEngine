#include "AudioSystem.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>

// c++
#include <algorithm>
#include <cmath>

Audio* Audio::instance_ = nullptr;

Audio* Audio::GetInstance() {

	if (instance_ == nullptr) {
		instance_ = new Audio();
	}
	return instance_;
}

void Audio::Finalize() {

	if (!instance_) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(instance_->mutex_);
		instance_->Unload();
		instance_->device_.Finalize();
		instance_->soundCache_.Finalize();
	}
	delete instance_;
	instance_ = nullptr;
}

void Audio::Init() {

	std::lock_guard<std::mutex> lock(mutex_);
	if (device_.IsInitialized()) {
		return;
	}
	soundCache_.Init();
	device_.Init();
	// 音声は必要時に読み込み、背景再生の希望値だけ先に取得する
	if (!settings_.Load(RuntimePaths::GetProjectSettingsPath(ConfigPaths::kAudio))) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "音声設定を読み込めません。既定値を使用します");
	}
}

bool Audio::SetPlayInBackground(bool enabled) {

	std::lock_guard<std::mutex> lock(mutex_);
	AudioSettings replacement = settings_;
	replacement.playInBackground = enabled;
	if (!replacement.Save(RuntimePaths::GetProjectSettingsPath(ConfigPaths::kAudio))) { return false; }
	settings_ = replacement;
	return true;
}

bool Audio::EnsureLoaded(const std::string& filename, AudioType type) {

	return EnsureLoaded(Algorithm::PathFromUTF8(filename), type);
}

bool Audio::EnsureLoaded(const std::filesystem::path& filename, AudioType type) {

	std::lock_guard<std::mutex> lock(mutex_);
	Assert::Call(device_.IsInitialized(), "Audio::EnsureLoadedより先にAudio::Initを呼び出してください");

	const std::string key = AudioSoundCache::NormalizeKey(Algorithm::PathToUTF8(filename));
	std::error_code error;
	if (!std::filesystem::exists(filename, error) || error) {
		return false;
	}

	soundCache_.Load(filename, type);
	return soundCache_.Find(key) != nullptr;
}

void Audio::Unload() {

	// サウンド停止、破棄
	for (auto& [key, vec] : activeVoices_) {
		for (auto& inst : vec) {
			if (!inst.voice) {
				continue;
			}

			inst.voice->Stop(0, XAUDIO2_COMMIT_NOW);
			inst.voice->FlushSourceBuffers();
			inst.voice = nullptr;
		}
	}
	activeVoices_.clear();

	// サウンド解放
	soundCache_.Clear();
}

//============================================================================
//	再生処理
//============================================================================
void Audio::Play(const std::string& name, float volume, AudioPlaybackOwner owner) {

	PlayInternal(name, true, volume, owner);
}

void Audio::PlayOneShot(const std::string& name, float volume, AudioPlaybackOwner owner) {

	PlayInternal(name, false, volume, owner);
}

uint64_t Audio::PlayManaged(const std::string& name, bool loop, float volume, AudioPlaybackOwner owner, float pitch) {

	return PlayInternal(name, loop, volume, owner, pitch);
}

uint64_t Audio::PlayInternal(const std::string& name, bool loop, float volume, AudioPlaybackOwner owner, float pitch) {

	std::lock_guard<std::mutex> lock(mutex_);
	Assert::Call(device_.IsInitialized(), "Audio::Playより先にAudio::Initを呼び出してください");

	const std::string key = AudioSoundCache::NormalizeKey(name);

	const AudioSoundData* sound = soundCache_.Find(key);
	if (!sound) {
		return 0;
	}

	// 終了したvoiceを掃除
	CleanupFinishedVoicesLocked(key);

	// SourceVoiceを新規作成
	IXAudio2SourceVoice* srcVoice = nullptr;

	HRESULT hr = device_.CreateSourceVoice(&srcVoice, sound->GetFormat());
	VoiceInstance inst{};
	inst.sound = soundCache_.GetSnapshot(key);
	inst.voice.reset(srcVoice);
	inst.owner = owner;
	if (owner == AudioPlaybackOwner::Game) { inst.playbackState = gamePlaybackState_; }
	if (FAILED(hr) || !inst.voice) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "Audio: Source Voiceを作成できません HRESULT={}", hr);
		return 0;
	}
	inst.normalMatrix = device_.GetOutputMatrix(*srcVoice, sound->GetFormat()->nChannels);

	inst.voiceID = nextVoiceID_++;
	inst.instanceVolume = volume;
	inst.loop = loop;
	inst.pitch = std::isfinite(pitch) ? std::clamp(pitch, -3.0f, 3.0f) : 1.0f;
	inst.cursor.reverse = inst.pitch < 0.0f;
	if (inst.cursor.reverse) { inst.reversePCM = ReverseAudioPCM(*inst.sound); }
	inst.playbackState.SetPauseReason(AudioPauseReason::ZeroPitch, inst.pitch == 0.0f);
	srcVoice->SetFrequencyRatio(std::max(std::abs(inst.pitch), XAUDIO2_MIN_FREQ_RATIO), XAUDIO2_COMMIT_NOW);
	if (!SubmitVoiceBuffersLocked(inst, 0)) { return 0; }

	ApplyVoiceVolumeLocked(key, inst);

	hr = inst.playbackState.IsPaused() ? S_OK : srcVoice->Start(0, XAUDIO2_COMMIT_NOW);
	if (FAILED(hr)) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "Audio: 再生を開始できません HRESULT={}", hr);
		return 0;
	}

	const uint64_t voiceID = inst.voiceID;
	activeVoices_[key].push_back(std::move(inst));
	return voiceID;
}
