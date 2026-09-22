#pragma once

//============================================================================
//	include
//============================================================================
#include "AudioDecoder.h"

// c++
#include <string>
#include <unordered_map>

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

		AudioSoundData* Find(const std::string& key);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		AudioDecoder decoder_;
		std::unordered_map<std::string, AudioSoundData> sounds_;

		//--------- functions ----------------------------------------------------

		// パスから音源タイプを選択
		AudioType GuessAudioTypeFromPath(const std::filesystem::path& p) const;
	};
}
