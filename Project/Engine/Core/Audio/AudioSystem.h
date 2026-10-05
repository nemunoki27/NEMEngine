#pragma once

//============================================================================
//	include
//============================================================================
#include "AudioDevice.h"
#include "AudioSoundCache.h"
#include "AudioVoiceOwner.h"
#include "AudioPlaybackState.h"
#include "AudioSettings.h"
#include "AudioPlaybackCursor.h"

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
		void Play(const std::string& name, float volume = 1.0f, AudioPlaybackOwner owner = AudioPlaybackOwner::Game);
		// サウンドを一度だけ再生
		void PlayOneShot(const std::string& name, float volume = 1.0f, AudioPlaybackOwner owner = AudioPlaybackOwner::Game);
		// サウンドをインスタンスID付きで再生
		uint64_t PlayManaged(const std::string& name, bool loop, float volume = 1.0f,
			AudioPlaybackOwner owner = AudioPlaybackOwner::Game, float pitch = 1.0f);
		// ファイルからサウンドを読み込む
		bool EnsureLoaded(const std::string& filename, AudioType type = AudioType::SE);
		bool EnsureLoaded(const std::filesystem::path& filename, AudioType type = AudioType::SE);

		// サウンドを停止
		void Stop(const std::string& name, AudioPlaybackOwner owner = AudioPlaybackOwner::Game);
		// ゲームの停止理由を変更し、Editor previewは継続する
		void SetGamePauseReason(AudioPauseReason reason, bool paused);
		// World終了時にUIのOneShotも終了する
		void StopGameVoices();
		// 再生インスタンスを停止
		void StopVoice(uint64_t voiceID);
		// 再生インスタンスを一時停止/再開し、voiceは破棄せず再生位置を保持する
		void PauseVoice(uint64_t voiceID);
		void ResumeVoice(uint64_t voiceID);
		// 再生インスタンスの音量を変更
		void SetVoiceVolume(uint64_t voiceID, float volume);
		// 再生位置を保持してループ設定を変更
		void SetVoiceLoop(uint64_t voiceID, bool loop);
		// 負値で逆再生、0で位置を保持する
		void SetVoicePitch(uint64_t voiceID, float pitch);
		// 受音位置とVoiceの空間設定を更新する
		void SetListener(const AudioListenerState& listener);
		void SetVoiceSpatial(uint64_t voiceID, const AudioSpatialState& spatial);
		// Project設定を保存できた場合だけ背景再生の希望値を更新する
		bool SetPlayInBackground(bool enabled);
		bool IsPlayInBackgroundEnabled() const { return settings_.playInBackground; }

		// 音量のセット
		void SetVolume(const std::string& name, float volume);

		// サウンドが再生中か
		bool IsPlaying(const std::string& name);
		// 再生インスタンスが再生中か
		bool IsVoicePlaying(uint64_t voiceID);
		// 再生インスタンスが存在するか
		bool IsVoiceAlive(uint64_t voiceID);
		// Voice生成後に進んだPCMサンプル数を取得する
		uint64_t GetVoiceSamplePosition(uint64_t voiceID);
		// 終了した再生インスタンスを破棄
		void CleanupFinishedVoices();

		//--------- accessor -----------------------------------------------------

		// マスター音量のセット
		void SetMasterVolume(float volume);

		// singleton
		static Audio* GetInstance();
		static Audio* TryGetInstance() { return instance_; }
		static void Finalize();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// 再生中インスタンス
		struct VoiceInstance {

			std::shared_ptr<const AudioSoundData> sound;
			std::vector<uint8_t> reversePCM;
			AudioVoiceOwner voice;
			uint64_t voiceID = 0;

			// 再生中の情報
			float instanceVolume = 1.0f;
			bool loop = false;
			AudioPlaybackState playbackState;
			AudioPlaybackOwner owner = AudioPlaybackOwner::Game;
			AudioSpatialState spatial;
			std::vector<float> normalMatrix;
			AudioPlaybackCursor cursor;
			float pitch = 1.0f;
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
		AudioPlaybackState gamePlaybackState_;
		AudioListenerState listener_;
		AudioSettings settings_;

		// 排他
		std::mutex mutex_;

		//--------- functions ----------------------------------------------------

		// サウンドデータを解放
		void Unload();

		// 共通再生
		uint64_t PlayInternal(const std::string& name, bool loop, float volume, AudioPlaybackOwner owner, float pitch = 1.0f);

		// 終了したVoiceを掃除
		void CleanupFinishedVoicesLocked(const std::string& key);

		// 全てのVoiceを掃除
		void CleanupAllFinishedVoicesLocked();

		// そのvoiceに最終音量を適用
		void ApplyVoiceVolumeLocked(const std::string& key, VoiceInstance& inst);
		// そのvoiceの再生位置からbufferを積み直す
		void RebuildVoiceBufferLocked(const AudioSoundData& sound, VoiceInstance& inst, bool loop);
		// Clip位置に対応するPCM Bufferを送信する
		bool SubmitVoiceBuffersLocked(VoiceInstance& inst, uint32_t position);
		// 符号変更時は現在位置を反転PCMへ対応付ける
		void ApplyVoicePitchLocked(VoiceInstance& inst, float pitch);

		Audio() = default;
		~Audio() = default;
		Audio(const Audio&) = delete;
		Audio& operator=(const Audio&) = delete;
	};
}
