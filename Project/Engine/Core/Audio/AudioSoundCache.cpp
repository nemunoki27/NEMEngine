#include "AudioSoundCache.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>

void AudioSoundCache::LoadAllSounds() {

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
			Load(filePath, type);
		}
	}
}

void AudioSoundCache::Load(const std::filesystem::path& filename, AudioType type) {

	const std::string key = NormalizeKey(Algorithm::PathToUTF8(filename));

	std::error_code error;
	auto writeTime = std::filesystem::last_write_time(filename, error);
	if (error) { return; }
	// 同じ入力を繰り返し復号しない
	auto found = sounds_.find(key);
	if (found != sounds_.end() && found->second.attempted && found->second.attemptedWriteTime == writeTime) {
		return;
	}
	// 失敗した入力も記録し、同じ壊れたファイルを繰り返し復号しない
	Entry& entry = sounds_[key];
	entry.attempted = true;
	entry.attemptedWriteTime = writeTime;

	const std::string ext = Algorithm::ToLower(filename.extension().string());

	AudioSoundData data{};
	// 読込失敗とメモリ不足では公開済みPCMを保持する
	try {
		if (ext == ".wav" || ext == ".wave") {
			data = decoder_.LoadWaveFile(filename);
		} else if (ext == ".mp3") {
			data = decoder_.LoadMP3File(filename);
		} else {
			Logger::Output(LogType::Engine, spdlog::level::warn, "未対応の音声形式です。WAVまたはMP3を使用してください");
			return;
		}
	} catch (const std::exception& failure) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "Audio: 読込に失敗しました path={} 内容={}",
			Algorithm::PathToUTF8(filename), failure.what());
		return;
	}

	// 読込失敗を有効なcacheとして公開しない
	if (data.formatBlob.size() < sizeof(WAVEFORMATEX) || data.pcmBuffer.empty()) {
		return;
	}
	// タイプと基準音量を設定して登録
	data.type = type;
	data.volume = 1.0f;
	// 成功後に公開し、旧PCMは再生中のVoiceへ残す
	Entry replacement{};
	replacement.sound = std::make_shared<const AudioSoundData>(std::move(data));
	replacement.writeTime = writeTime;
	replacement.attemptedWriteTime = writeTime;
	replacement.attempted = true;
	replacement.volume = entry.volume;
	sounds_.insert_or_assign(key, std::move(replacement));
}

const AudioSoundData* AudioSoundCache::Find(const std::string& key) const {

	auto found = sounds_.find(key);
	return found != sounds_.end() ? found->second.sound.get() : nullptr;
}

std::shared_ptr<const AudioSoundData> AudioSoundCache::GetSnapshot(const std::string& key) const {

	auto found = sounds_.find(key);
	return found != sounds_.end() ? found->second.sound : nullptr;
}

float AudioSoundCache::GetVolume(const std::string& key) const {

	auto found = sounds_.find(key);
	return found != sounds_.end() ? found->second.volume : 0.0f;
}

void AudioSoundCache::SetVolume(const std::string& key, float volume) {

	auto found = sounds_.find(key);
	if (found != sounds_.end()) { found->second.volume = std::clamp(volume, 0.0f, 1.0f); }
}

AudioType AudioSoundCache::GuessAudioTypeFromPath(const std::filesystem::path& p) const {

	// パスの構成要素に "BGM" or "SE" が含まれているかで判定
	for (const auto& part : p) {

		std::string s = Algorithm::PathToUTF8(part);
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

std::string AudioSoundCache::NormalizeKey(const std::string& nameOrPath) {

	// 同名ファイルを別の音声として扱う
	std::error_code error;
	auto path = std::filesystem::weakly_canonical(Algorithm::PathFromUTF8(nameOrPath), error);
	if (error) { path = Algorithm::PathFromUTF8(nameOrPath).lexically_normal(); }
	return Algorithm::ToLower(Algorithm::PathToUTF8(path));
}
