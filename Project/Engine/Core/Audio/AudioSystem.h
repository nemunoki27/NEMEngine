#pragma once

//============================================================================
//	include
//============================================================================
#include "AudioDevice.h"
#include "AudioSoundCache.h"

// c++
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <filesystem>
#include <cstdint>

namespace Engine {

	//============================================================================
	//	Audio class
	//	音の管理を行い、再生と停止を提供するクラス
	//============================================================================
	class Audio {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		// 初期化
		void Init();

		// サウンドをループ再生
		void Play(const std::string& name, float volume = 1.0f);
		// サウンドを一度だけ再生
		void PlayOneShot(const std::string& name, float volume = 1.0f);
		// サウンドをインスタンスID付きで再生
		uint64_t PlayManaged(const std::string& name, bool loop, float volume = 1.0f);
		// ファイルからサウンドを読み込む
		bool EnsureLoaded(const std::string& filename, AudioType type = AudioType::SE);
		bool EnsureLoaded(const std::filesystem::path& filename, AudioType type = AudioType::SE);

		// サウンドを停止
		void Stop(const std::string& name);
		// 再生インスタンスを停止
		void StopVoice(uint64_t voiceID);
		// 再生インスタンスを一時停止/再開し、voiceは破棄せず再生位置を保持する
		void PauseVoice(uint64_t voiceID);
		void ResumeVoice(uint64_t voiceID);
		// 再生インスタンスの音量を変更
		void SetVoiceVolume(uint64_t voiceID, float volume);
		// 再生位置を保持してループ設定を変更
		void SetVoiceLoop(uint64_t voiceID, bool loop);

		// 音量のセット
		void SetVolume(const std::string& name, float volume);

		// サウンドが再生中か
		bool IsPlaying(const std::string& name);
		// 再生インスタンスが再生中か
		bool IsVoicePlaying(uint64_t voiceID);
		// 再生インスタンスが存在するか
		bool IsVoiceAlive(uint64_t voiceID);
		// 終了した再生インスタンスを破棄
		void CleanupFinishedVoices();

		//--------- accessor -----------------------------------------------------

		// マスター音量のセット
		void SetMasterVolume(float volume) { masterVolume_ = volume; }

		// singleton
		static Audio* GetInstance();
		static void Finalize();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// 再生中インスタンス
		struct VoiceInstance {

			IXAudio2SourceVoice* voice = nullptr;
			uint64_t voiceID = 0;

			// 再生中の情報
			float instanceVolume = 1.0f;
			bool loop = false;
			bool paused = false;
		};

		//--------- variables ----------------------------------------------------

		static Audio* instance_;

		// マスター音量、全ての音に適用される値
		float masterVolume_ = 1.0f;

		// XAudio2
		AudioDevice device_;

		// 読み込んだサウンド
		AudioSoundCache soundCache_;

		// 再生中の音リソース
		std::unordered_map<std::string, std::vector<VoiceInstance>> activeVoices_{};
		uint64_t nextVoiceID_ = 1;

		// 排他
		std::mutex mutex_;

		//--------- functions ----------------------------------------------------

		// サウンドデータを解放
		void Unload();

		// 共通再生
		uint64_t PlayInternal(const std::string& name, bool loop, float volume);

		// 終了したVoiceを掃除
		void CleanupFinishedVoicesLocked(const std::string& key);

		// 全てのVoiceを掃除
		void CleanupAllFinishedVoicesLocked();

		// そのvoiceに最終音量を適用
		void ApplyVoiceVolumeLocked(const std::string& key, VoiceInstance& inst);
		// そのvoiceの再生位置からbufferを積み直す
		void RebuildVoiceBufferLocked(const AudioSoundData& sound, VoiceInstance& inst, bool loop);

		Audio() = default;
		~Audio() = default;
		Audio(const Audio&) = delete;
		Audio& operator=(const Audio&) = delete;
	};
}
