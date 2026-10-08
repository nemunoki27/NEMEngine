#include "AudioDecoderTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Audio/AudioWaveReader.h>
#include <Engine/Core/Audio/AudioSoundCache.h>
#include <Engine/Core/Audio/AudioPlaybackState.h>
#include <Engine/Core/Audio/AudioSpatialState.h>
#include <Engine/Core/Audio/AudioSettings.h>
#include <Engine/Core/Audio/AudioPlaybackCursor.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <fstream>
#include <chrono>
#include <vector>

namespace {

	void AppendInteger(std::vector<uint8_t>& bytes, uint32_t value, uint32_t width) {

		for (uint32_t i = 0; i < width; ++i) {
			bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
		}
	}

	void AppendChunk(std::vector<uint8_t>& bytes, const char* id, const std::vector<uint8_t>& contents) {

		bytes.insert(bytes.end(), id, id + 4);
		AppendInteger(bytes, static_cast<uint32_t>(contents.size()), 4);
		bytes.insert(bytes.end(), contents.begin(), contents.end());
		if (contents.size() & 1) { bytes.push_back(0); }
	}

	std::vector<uint8_t> MakeWave(bool dataFirst) {

		// 16byteのfmtと奇数長の未知チャンクを含むPCMを作る
		std::vector<uint8_t> format;
		AppendInteger(format, 1, 2);
		AppendInteger(format, 1, 2);
		AppendInteger(format, 44100, 4);
		AppendInteger(format, 88200, 4);
		AppendInteger(format, 2, 2);
		AppendInteger(format, 16, 2);
		std::vector<uint8_t> chunks;
		AppendChunk(chunks, "JUNK", { 1, 2, 3 });
		if (dataFirst) { AppendChunk(chunks, "data", { 0, 0, 255, 127 }); }
		AppendChunk(chunks, "fmt ", format);
		if (!dataFirst) { AppendChunk(chunks, "data", { 0, 0, 255, 127 }); }
		std::vector<uint8_t> result{ 'R', 'I', 'F', 'F' };
		AppendInteger(result, static_cast<uint32_t>(chunks.size() + 4), 4);
		result.insert(result.end(), { 'W', 'A', 'V', 'E' });
		result.insert(result.end(), chunks.begin(), chunks.end());
		return result;
	}

	bool TestSoundGenerations(const std::filesystem::path& root) {

		// 同名ファイルを別のPCMとして公開する
		std::filesystem::create_directories(root / "A");
		std::filesystem::create_directories(root / "B");
		auto firstPath = root / "A" / "Same.wav";
		auto secondPath = root / "B" / "Same.wav";
		auto bytes = MakeWave(false);
		auto save = [&](const std::filesystem::path& path, const std::vector<uint8_t>& contents) {
			std::ofstream output(path, std::ios::binary);
			output.write(reinterpret_cast<const char*>(contents.data()), contents.size());
			return static_cast<bool>(output);
		};
		if (!save(firstPath, bytes) || !save(secondPath, bytes)) return false;
		Engine::AudioSoundCache cache;
		cache.Load(firstPath, Engine::AudioType::SE);
		cache.Load(secondPath, Engine::AudioType::SE);
		auto firstKey = Engine::AudioSoundCache::NormalizeKey(Engine::Algorithm::PathToUTF8(firstPath));
		auto secondKey = Engine::AudioSoundCache::NormalizeKey(Engine::Algorithm::PathToUTF8(secondPath));
		auto first = cache.GetSnapshot(firstKey);
		auto second = cache.GetSnapshot(secondKey);
		if (!first || !second || first == second || firstKey == secondKey) return false;
		cache.SetVolume(firstKey, 0.3f);
		auto writeTime = std::filesystem::last_write_time(firstPath);
		bytes.back() = 0;
		if (!save(firstPath, bytes)) return false;
		std::filesystem::last_write_time(firstPath, writeTime + std::chrono::seconds(1));
		cache.Load(firstPath, Engine::AudioType::SE);
		auto replacement = cache.GetSnapshot(firstKey);
		if (!replacement || replacement == first || first->pcmBuffer.back() != 127 ||
			replacement->pcmBuffer.back() != 0 || cache.GetVolume(firstKey) != 0.3f) return false;
		// 壊れた再読込では旧世代を保持する
		if (!save(firstPath, { 1, 2, 3 })) return false;
		std::filesystem::last_write_time(firstPath, writeTime + std::chrono::seconds(2));
		cache.Load(firstPath, Engine::AudioType::SE);
		if (cache.GetSnapshot(firstKey) != replacement) return false;
		cache.Clear();
		return first->pcmBuffer.back() == 127 && replacement->pcmBuffer.back() == 0;
	}
}

bool NEMTests::TestAudioWaveReader() {

	// ループ再構築を重ねてもClip位置がずれない
	Engine::AudioPlaybackCursor cursor{ 100, 50, false };
	if (cursor.Resolve(125, 200) != 75 || cursor.Resolve(325, 200) != 75) return false;
	cursor.reverse = true;
	if (cursor.Resolve(125, 200) != 25 || cursor.Resolve(175, 200) != 175) return false;

	// Cameraによらず距離と減衰方式だけで音量を求める
	Engine::AudioListenerState listener;
	listener.active = true;
	Engine::AudioSpatialState source;
	source.position = { 0.0f, 0.0f, 2.0f };
	if (Engine::CalculateAudioDistanceGain(source, listener) != 0.5f) return false;
	source.maxDistance = 2.0f;
	source.position.z = 4.0f;
	if (Engine::CalculateAudioDistanceGain(source, listener) != 0.25f) return false;
	source.rolloffMode = Engine::AudioRolloffMode::Linear;
	if (Engine::CalculateAudioDistanceGain(source, listener) != 0.0f) return false;
	source.position.z = 1.0f;
	if (Engine::CalculateAudioDistanceGain(source, listener) != 1.0f) return false;
	listener.active = false;
	if (Engine::CalculateAudioDistanceGain(source, listener) != 0.0f) return false;

	// EditorのResumeでScriptのPauseを解除しない
	Engine::AudioPlaybackState state;
	if (!state.SetPauseReason(Engine::AudioPauseReason::Script, true) ||
		state.SetPauseReason(Engine::AudioPauseReason::Editor, true) ||
		state.SetPauseReason(Engine::AudioPauseReason::Editor, false) || !state.IsPaused() ||
		!state.SetPauseReason(Engine::AudioPauseReason::Script, false)) return false;
	if (!state.SetPauseReason(Engine::AudioPauseReason::Background, true) ||
		state.SetPauseReason(Engine::AudioPauseReason::Editor, true) ||
		state.SetPauseReason(Engine::AudioPauseReason::Background, false) || !state.IsPaused() ||
		!state.SetPauseReason(Engine::AudioPauseReason::Editor, false) || state.IsPaused()) return false;

	TestDirectory directory("AudioWave");
	// 設定の不正値では現在の希望値を変更しない
	Engine::AudioSettings settings;
	auto settingsPath = directory.GetPath() / "Audio.json";
	if (!settings.Load(settingsPath) || !settings.playInBackground) return false;
	settings.playInBackground = false;
	if (!settings.Save(settingsPath)) return false;
	Engine::AudioSettings loaded;
	if (!loaded.Load(settingsPath) || loaded.playInBackground) return false;
	{
		std::ofstream invalid(settingsPath);
		invalid << "{\"playInBackground\":42}";
	}
	if (loaded.Load(settingsPath) || loaded.playInBackground) return false;
	const auto path = directory.GetPath() / L"日本語.wave";
	std::string error;
	auto save = [&](const std::vector<uint8_t>& bytes) {
		std::ofstream output(path, std::ios::binary);
		output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		return static_cast<bool>(output);
	};
	for (bool dataFirst : { false, true }) {
		if (!save(MakeWave(dataFirst))) { return false; }
		auto sound = Engine::ReadAudioWave(path, error);
		if (!sound || !error.empty() || sound->formatBlob.size() != sizeof(WAVEFORMATEX) ||
			sound->GetFormat()->cbSize != 0 || sound->GetFormat()->nBlockAlign != 2 ||
			sound->pcmBuffer != std::vector<uint8_t>{ 0, 0, 255, 127 }) { return false; }
	}
	// RIFFと内部チャンクの破損は、巨大なメモリ確保前に拒否する
	auto bytes = MakeWave(false);
	bytes.pop_back();
	if (!save(bytes) || Engine::ReadAudioWave(path, error) || error.empty()) { return false; }
	bytes = MakeWave(false);
	bytes[16] = bytes[17] = bytes[18] = bytes[19] = 255;
	if (!save(bytes) || Engine::ReadAudioWave(path, error) || error.empty()) { return false; }
	bytes = MakeWave(false);
	// fmtのblockAlignを壊す
	bytes[44] = 0;
	if (!save(bytes) || Engine::ReadAudioWave(path, error) || error.empty()) { return false; }
	return TestSoundGenerations(directory.GetPath());
}
