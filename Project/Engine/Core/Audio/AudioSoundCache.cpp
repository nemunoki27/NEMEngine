#include "AudioSoundCache.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

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

	const std::string key = Algorithm::PathToUTF8(filename.stem());

	// 既に読み込み済みなら上書きしない
	if (sounds_.find(key) != sounds_.end()) {
		return;
	}

	const std::string ext = Algorithm::ToLower(filename.extension().string());

	AudioSoundData data{};
	// 語尾で判別して読み込み
	if (ext == ".wav" || ext == ".wave") {

		data = decoder_.LoadWaveFile(filename);
	} else if (ext == ".mp3") {

		data = decoder_.LoadMP3File(filename);
	} else {

		Assert::Call(false, "未対応の音声形式です。WAVまたはMP3を使用してください");
	}

	// タイプと基準音量を設定して登録
	data.type = type;
	data.volume = 1.0f;
	sounds_.emplace(key, std::move(data));
}

AudioSoundData* AudioSoundCache::Find(const std::string& key) {
	auto it = sounds_.find(key);
	if (it == sounds_.end()) {
		return nullptr;
	}
	return &it->second;
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

	const std::filesystem::path p = Algorithm::PathFromUTF8(nameOrPath);
	if (p.has_extension() || nameOrPath.find('/') != std::string::npos || nameOrPath.find('\\') != std::string::npos) {
		return Algorithm::PathToUTF8(p.stem());
	}
	return nameOrPath;
}
