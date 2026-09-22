#include "AudioSystem.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>

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
	soundCache_.LoadAllSounds();
}

bool Audio::EnsureLoaded(const std::string& filename, AudioType type) {

	return EnsureLoaded(Algorithm::PathFromUTF8(filename), type);
}

bool Audio::EnsureLoaded(const std::filesystem::path& filename, AudioType type) {

	std::lock_guard<std::mutex> lock(mutex_);
	Assert::Call(device_.IsInitialized(), "Audio::EnsureLoadedより先にAudio::Initを呼び出してください");

	const std::string key = Algorithm::PathToUTF8(filename.stem());
	if (soundCache_.Find(key) != nullptr) {
		return true;
	}
	if (!std::filesystem::exists(filename)) {
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
			inst.voice->DestroyVoice();
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
void Audio::Play(const std::string& name, float volume) {

	PlayInternal(name, true, volume);
}

void Audio::PlayOneShot(const std::string& name, float volume) {

	PlayInternal(name, false, volume);
}

uint64_t Audio::PlayManaged(const std::string& name, bool loop, float volume) {

	return PlayInternal(name, loop, volume);
}

uint64_t Audio::PlayInternal(const std::string& name, bool loop, float volume) {

	std::lock_guard<std::mutex> lock(mutex_);
	Assert::Call(device_.IsInitialized(), "Audio::Playより先にAudio::Initを呼び出してください");

	const std::string key = AudioSoundCache::NormalizeKey(name);

	AudioSoundData* sound = soundCache_.Find(key);
	if (!sound) {
		return 0;
	}

	// 終了したvoiceを掃除
	CleanupFinishedVoicesLocked(key);

	// SourceVoiceを新規作成
	IXAudio2SourceVoice* srcVoice = nullptr;

	HRESULT hr = device_.CreateSourceVoice(&srcVoice, sound->GetFormat());
	Assert::Call(SUCCEEDED(hr), "XAudio2のソースボイス作成に失敗しました");
	Assert::Call(srcVoice != nullptr, "XAudio2のソースボイスが作成されていません");

	XAUDIO2_BUFFER buf{};
	buf.pAudioData = sound->GetPCM();
	buf.AudioBytes = sound->GetPCMBytes();
	buf.Flags = XAUDIO2_END_OF_STREAM;

	if (loop) {
		buf.LoopCount = XAUDIO2_LOOP_INFINITE;
	} else {
		buf.LoopCount = 0;
	}

	hr = srcVoice->SubmitSourceBuffer(&buf);
	Assert::Call(SUCCEEDED(hr), "XAudio2へ音声バッファを送信できませんでした");

	VoiceInstance inst{};
	inst.voice = srcVoice;
	inst.voiceID = nextVoiceID_++;
	inst.instanceVolume = volume;
	inst.loop = loop;

	ApplyVoiceVolumeLocked(key, inst);

	hr = srcVoice->Start(0, XAUDIO2_COMMIT_NOW);
	Assert::Call(SUCCEEDED(hr), "XAudio2のソースボイスを開始できませんでした");

	const uint64_t voiceID = inst.voiceID;
	activeVoices_[key].push_back(inst);
	return voiceID;
}
