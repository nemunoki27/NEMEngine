#include "AudioSystem.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// windows
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

//============================================================================
//	Audio classMethods
//============================================================================
namespace {

	// UTF-8/ANSI -> Wide
	static std::wstring ToWideString(const std::string& s) {

		if (s.empty()) {
			return {};
		}

		// まずUTF-8を試す
		int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, nullptr, 0);
		if (len > 0) {
			std::wstring ws;
			ws.resize(static_cast<size_t>(len));
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, ws.data(), len);
			if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
			return ws;
		}

		// ダメならANSI
		len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
		if (len > 0) {
			std::wstring ws;
			ws.resize(static_cast<size_t>(len));
			MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, ws.data(), len);
			if (!ws.empty() && ws.back() == L'\0') ws.pop_back();
			return ws;
		}
		return {};
	}
}

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
		// 排他
		std::lock_guard<std::mutex> lock(instance_->mutex_);
		// サウンドデータを解放
		instance_->Unload();

		// XAudio2voiceを破棄
		if (instance_->masteringVoice_) {

			instance_->masteringVoice_->DestroyVoice();
			instance_->masteringVoice_ = nullptr;
		}
		if (instance_->xAudio2_) {

			instance_->xAudio2_->StopEngine();
			instance_->xAudio2_.Reset();
		}

		// MediaFoundation終了
		if (instance_->mfStarted_) {

			MFShutdown();
			instance_->mfStarted_ = false;
		}
	}

	delete instance_;
	instance_ = nullptr;
}

void Audio::Init() {

	std::lock_guard<std::mutex> lock(mutex_);

	// 既に初期化済みなら何もしない
	if (xAudio2_ && masteringVoice_) {
		return;
	}

	// MediaFoundation初期化
	HRESULT hr = MFStartup(MF_VERSION);
	if (SUCCEEDED(hr)) {
		mfStarted_ = true;
	} else {
		Assert::Call(false, "MFStartup failed");
	}

	// XAudio2初期化
	hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(hr));

	hr = xAudio2_->CreateMasteringVoice(&masteringVoice_);
	assert(SUCCEEDED(hr));

	hr = xAudio2_->StartEngine();
	assert(SUCCEEDED(hr));

	// Sounds/配下のファイルをすべて読み込み
	LoadAllSounds();
}

void Audio::LoadAllSounds() {

	// Engine側とGame側のSounds配下を読み込む
	std::vector<std::filesystem::path> roots = {
		RuntimePaths::GetEngineAssetPath("Sounds"),
		RuntimePaths::GetGameRoot() / "GameAssets" / "Sounds",
	};

	for (const std::filesystem::path& root : roots) {

		std::error_code ec;
		if (!std::filesystem::exists(root, ec)) {
			// 無いなら何もしない
			continue;
		}

		// recursiveに走査
		for (std::filesystem::recursive_directory_iterator it(root, ec), end;
			it != end && !ec; it.increment(ec)) {

			const auto& entry = *it;

			// ディレクトリはスキップ
			if (!entry.is_regular_file(ec)) {
				continue;
			}

			const std::filesystem::path filePath = entry.path();
			std::string ext = Algorithm::ToLower(filePath.extension().string());

			// 対応拡張子のみ
			const bool supported =
				(ext == ".wav") || (ext == ".wave") || (ext == ".mp3");

			if (!supported) {
				continue;
			}

			AudioType type = GuessAudioTypeFromPath(filePath);

			// string化
			std::string fullpath = filePath.string();
			Load(fullpath, type);
		}
	}
}

void Audio::Load(const std::string& filename, AudioType type) {

	Assert::Call(xAudio2_ && masteringVoice_, "Audio::Init() must be called before Load()");

	const std::string key = NormalizeKey(filename);

	// 既に読み込み済みなら上書きしない
	if (sounds_.find(key) != sounds_.end()) {
		return;
	}

	std::filesystem::path p(filename);
	std::string ext = Algorithm::ToLower(p.extension().string());

	SoundData data{};
	// 語尾で判別して読み込み
	if (ext == ".wav" || ext == ".wave") {

		data = LoadWaveFile(filename);
	} else if (ext == ".mp3") {

		data = LoadMp3FileWithMediaFoundation(filename);
	} else {

		Assert::Call(false, "Unsupported audio format. Use WAV or MP3.");
	}

	// タイプと基準音量を設定して登録
	data.type = type;
	data.volume = 1.0f;
	sounds_.emplace(key, std::move(data));
}

bool Audio::EnsureLoaded(const std::string& filename, AudioType type) {

	std::lock_guard<std::mutex> lock(mutex_);
	Assert::Call(xAudio2_ && masteringVoice_, "Audio::Init() must be called before EnsureLoaded()");

	const std::string key = NormalizeKey(filename);
	if (sounds_.find(key) != sounds_.end()) {
		return true;
	}
	if (!std::filesystem::exists(filename)) {
		return false;
	}

	Load(filename, type);
	return sounds_.find(key) != sounds_.end();
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
	sounds_.clear();
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
	Assert::Call(xAudio2_ && masteringVoice_, "Audio::Init() must be called before Play()");

	const std::string key = NormalizeKey(name);

	SoundData* sound = FindSoundLocked(key);
	if (!sound) {
		return 0;
	}

	// 終了したvoiceを掃除
	CleanupFinishedVoicesLocked(key);

	// SourceVoiceを新規作成
	IXAudio2SourceVoice* srcVoice = nullptr;

	HRESULT hr = xAudio2_->CreateSourceVoice(&srcVoice, sound->GetFormat(),
		0, XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, nullptr, nullptr);
	assert(SUCCEEDED(hr));
	assert(srcVoice);

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
	assert(SUCCEEDED(hr));

	VoiceInstance inst{};
	inst.voice = srcVoice;
	inst.voiceID = nextVoiceID_++;
	inst.instanceVolume = volume;
	inst.loop = loop;

	ApplyVoiceVolumeLocked(key, inst);

	hr = srcVoice->Start(0, XAUDIO2_COMMIT_NOW);
	assert(SUCCEEDED(hr));

	const uint64_t voiceID = inst.voiceID;
	activeVoices_[key].push_back(inst);
	return voiceID;
}

//============================================================================
//	停止処理
//============================================================================
void Audio::Stop(const std::string& name) {

	std::lock_guard<std::mutex> lock(mutex_);
	const std::string key = NormalizeKey(name);

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
			if (inst.voiceID == voiceID && inst.voice) {
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
			if (inst.voiceID == voiceID && inst.voice) {
				inst.voice->Start(0, XAUDIO2_COMMIT_NOW);
			}
		}
	}
}

//============================================================================
//	マスター音量、状態取得
//============================================================================
void Audio::SetVolume(const std::string& name, float volume) {

	std::lock_guard<std::mutex> lock(mutex_);
	const std::string key = NormalizeKey(name);

	SoundData* sound = FindSoundLocked(key);
	if (!sound) {
		return;
	}

	sound->volume = std::clamp(volume, 0.0f, 1.0f);

	// 再生中の全インスタンスにも反映
	auto it = activeVoices_.find(key);
	if (it == activeVoices_.end()) {
		return;
	}

	CleanupFinishedVoicesLocked(key);

	for (auto& inst : it->second) {
		ApplyVoiceVolumeLocked(key, inst);
	}
}

bool Audio::IsPlaying(const std::string& name) {

	std::lock_guard<std::mutex> lock(mutex_);
	const std::string key = NormalizeKey(name);

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
				return true;
			}
		}
	}
	return false;
}

//============================================================================
//	Cleanup、終了したサウンドを破棄
//============================================================================
void Audio::CleanupFinishedVoicesLocked(const std::string& key) {

	auto it = activeVoices_.find(key);
	if (it == activeVoices_.end()) {
		return;
	}

	auto& vec = it->second;

	vec.erase(std::remove_if(vec.begin(), vec.end(),
		[](VoiceInstance& inst) {
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
		vec.end()
	);

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

//============================================================================
//	音量適用
//============================================================================
void Audio::ApplyVoiceVolumeLocked(const std::string& key, VoiceInstance& inst) {

	if (!inst.voice) {
		return;
	}

	SoundData* sound = FindSoundLocked(key);
	if (!sound) {
		return;
	}

	const float mv = std::clamp(masterVolume_, 0.0f, 1.0f);
	const float sv = std::clamp(sound->volume, 0.0f, 1.0f);
	const float iv = std::clamp(inst.instanceVolume, 0.0f, 1.0f);

	const float finalVol = mv * sv * iv;
	inst.voice->SetVolume(finalVol, XAUDIO2_COMMIT_NOW);
}

Audio::SoundData* Audio::FindSoundLocked(const std::string& key) {
	auto it = sounds_.find(key);
	if (it == sounds_.end()) {
		return nullptr;
	}
	return &it->second;
}

AudioType Audio::GuessAudioTypeFromPath(const std::filesystem::path& p) const {

	// パスの構成要素に "BGM" or "SE" が含まれているかで判定
	for (const auto& part : p) {

		std::string s = part.string();
		s = Algorithm::ToLower(s);

		if (s == "bgm") {
			return AudioType::BGM;
		}
		if (s == "se") {
			return AudioType::SE;
		}
	}
	return AudioType::SE;
}

std::string Audio::NormalizeKey(const std::string& nameOrPath) const {

	std::filesystem::path p(nameOrPath);
	if (p.has_extension() || nameOrPath.find('/') != std::string::npos || nameOrPath.find('\\') != std::string::npos) {
		return p.stem().string();
	}
	return nameOrPath;
}

Audio::SoundData Audio::LoadWaveFile(const std::string& filename) {

	std::ifstream file(filename, std::ios::binary);
	assert(file.is_open());

	RiffHeader riff{};
	file.read(reinterpret_cast<char*>(&riff), sizeof(riff));

	assert(std::strncmp(riff.chunk.id, "RIFF", 4) == 0);
	assert(std::strncmp(riff.type, "WAVE", 4) == 0);

	ChunkHeader ch{};
	bool fmtFound = false;
	bool dataFound = false;

	std::vector<uint8_t> fmtBlob;
	std::vector<uint8_t> pcm;

	while (file.read(reinterpret_cast<char*>(&ch), sizeof(ch))) {
		if (std::strncmp(ch.id, "fmt ", 4) == 0) {
			assert(ch.size >= 16);

			fmtBlob.resize(static_cast<size_t>(ch.size));
			file.read(reinterpret_cast<char*>(fmtBlob.data()), ch.size);
			fmtFound = true;
		} else if (std::strncmp(ch.id, "data", 4) == 0) {
			pcm.resize(static_cast<size_t>(ch.size));
			file.read(reinterpret_cast<char*>(pcm.data()), ch.size);
			dataFound = true;
		} else {
			// それ以外はスキップ
			file.seekg(ch.size, std::ios_base::cur);
		}

		if (fmtFound && dataFound) break;
	}

	file.close();
	assert(fmtFound && dataFound);
	assert(!fmtBlob.empty());
	assert(!pcm.empty());

	SoundData sd{};
	sd.formatBlob = std::move(fmtBlob);
	sd.pcmBuffer = std::move(pcm);
	return sd;
}

Audio::SoundData Audio::LoadMp3FileWithMediaFoundation(const std::string& filename) {

	Assert::Call(mfStarted_, "MF must be started in Audio::Init()");

	const std::wstring wpath = ToWideString(filename);
	assert(!wpath.empty());

	ComPtr<IMFSourceReader> reader;
	HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, &reader);
	assert(SUCCEEDED(hr));

	// 出力をPCMに指定
	ComPtr<IMFMediaType> outType;
	hr = MFCreateMediaType(&outType);
	assert(SUCCEEDED(hr));

	hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	assert(SUCCEEDED(hr));

	hr = outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	assert(SUCCEEDED(hr));

	hr = outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	assert(SUCCEEDED(hr));

	constexpr DWORD kAudioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

	// SetCurrentMediaType
	hr = reader->SetCurrentMediaType(kAudioStream, nullptr, outType.Get());
	assert(SUCCEEDED(hr));

	// GetCurrentMediaType
	ComPtr<IMFMediaType> currentType;
	hr = reader->GetCurrentMediaType(kAudioStream, &currentType);
	assert(SUCCEEDED(hr));

	WAVEFORMATEX* wfx = nullptr;
	UINT32 wfxSize = 0;
	hr = MFCreateWaveFormatExFromMFMediaType(currentType.Get(), &wfx, &wfxSize, MFWaveFormatExConvertFlag_Normal);
	assert(SUCCEEDED(hr));
	assert(wfx && wfxSize > 0);

	std::vector<uint8_t> fmtBlob;
	fmtBlob.resize(static_cast<size_t>(wfxSize));
	std::memcpy(fmtBlob.data(), wfx, wfxSize);
	CoTaskMemFree(wfx);

	// サンプルを最後まで読む
	std::vector<uint8_t> pcm;
	for (;;) {

		DWORD streamIndex = 0;
		DWORD flags = 0;
		LONGLONG timestamp = 0;
		ComPtr<IMFSample> sample;

		hr = reader->ReadSample(
			kAudioStream,
			0,
			&streamIndex,
			&flags,
			&timestamp,
			&sample
		);
		assert(SUCCEEDED(hr));
		if (flags & static_cast<DWORD>(MF_SOURCE_READERF_ENDOFSTREAM)) {
			break;
		}

		if (!sample) continue;

		ComPtr<IMFMediaBuffer> buffer;
		hr = sample->ConvertToContiguousBuffer(&buffer);
		assert(SUCCEEDED(hr));

		BYTE* data = nullptr;
		DWORD maxLen = 0;
		DWORD curLen = 0;
		hr = buffer->Lock(&data, &maxLen, &curLen);
		assert(SUCCEEDED(hr));

		const size_t oldSize = pcm.size();
		pcm.resize(oldSize + curLen);
		std::memcpy(pcm.data() + oldSize, data, curLen);

		buffer->Unlock();
	}

	assert(!fmtBlob.empty());
	assert(!pcm.empty());

	SoundData sd{};
	sd.formatBlob = std::move(fmtBlob);
	sd.pcmBuffer = std::move(pcm);
	return sd;
}
