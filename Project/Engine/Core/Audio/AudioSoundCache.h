#pragma once

//============================================================================
//	include
//============================================================================
#include "AudioDecoder.h"

// c++
#include <string>
#include <unordered_map>
#include <memory>

namespace Engine {

	//============================================================================
	//	AudioSoundCache class
	//	名前に対応する音声データと復号処理を所有する
	//============================================================================
	class AudioSoundCache {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		// 復号処理を初期化
		void Init() { decoder_.Init(); }
		// 復号処理を終了
		void Finalize() { decoder_.Finalize(); }
		// 標準の音声フォルダを読み込む
		void LoadAllSounds();
		// 未登録の音声を読み込む
		void Load(const std::filesystem::path& filename, AudioType type);
		// 音声データを解放
		void Clear() { sounds_.clear(); }
		// 名前とパスを共通キーへ変換
		static std::string NormalizeKey(const std::string& nameOrPath);

		//--------- accessor -----------------------------------------------------

		const AudioSoundData* Find(const std::string& key) const;
		std::shared_ptr<const AudioSoundData> GetSnapshot(const std::string& key) const;
		float GetVolume(const std::string& key) const;
		void SetVolume(const std::string& key, float volume);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		AudioDecoder decoder_;
		struct Entry {

			// 再生中のVoiceも共有するPCM世代
			std::shared_ptr<const AudioSoundData> sound;
			std::filesystem::file_time_type writeTime{};
			std::filesystem::file_time_type attemptedWriteTime{};
			bool attempted = false;
			float volume = 1.0f;
		};
		std::unordered_map<std::string, Entry> sounds_;

		//--------- functions ----------------------------------------------------

		// パスから音源タイプを選択
		AudioType GuessAudioTypeFromPath(const std::filesystem::path& p) const;
	};
}
