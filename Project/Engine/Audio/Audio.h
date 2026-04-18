#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/ComPtr.h>

// directX
#include <xaudio2.h>
// c++
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <filesystem>
#include <cstdint>
#include <cassert>
#include <fstream>
#include <algorithm>

namespace Engine {

	// 音源タイプ
	enum class AudioType {

		SE,
		BGM,
	};

	//============================================================================
	//	Audio class
	//	音の管理を行い、再生と停止を提供するクラス
	//	対応フォーマット: WAVE、MP3
	//============================================================================
	class Audio {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 初期化
		void Init();

		// エディター
		void ImGui();

		// サウンドをループ再生
		void Play(const std::string& name, float volume = 1.0f);
		// サウンドを一度だけ再生
		void PlayOneShot(const std::string& name, float volume = 1.0f);

		// サウンドを停止
		void Stop(const std::string& name);

		// 音量のセット
		void SetVolume(const std::string& name, float volume);

		// サウンドが再生中か
		bool IsPlaying(const std::string& name);

		//--------- accessor -----------------------------------------------------

		// マスター音量のセット
		void SetMasterVolume(float volume) { masterVolume_ = volume; }

		// singleton
		static Audio* GetInstance();
		static void Finalize();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// チャンク
		struct ChunkHeader {
			char id[4];
			int32_t size;
		};

		// RIFFチャンク
		struct RiffHeader {
			ChunkHeader chunk;
			char type[4];
		};

		// 音声データ
		struct SoundData {

			// フォーマット情報
			std::vector<uint8_t> formatBlob;

			// PCMデータ
			std::vector<uint8_t> pcmBuffer;

			// 音源タイプ
			AudioType type{};
			// サウンドの基準音量
			float volume = 1.0f;

			// キャスト用関数
			const WAVEFORMATEX* GetFormat() const { return reinterpret_cast<const WAVEFORMATEX*>(formatBlob.data()); }
			const BYTE* GetPCM() const { return reinterpret_cast<const BYTE*>(pcmBuffer.data()); }
			uint32_t GetPCMBytes() const { return static_cast<uint32_t>(pcmBuffer.size()); }
		};

		// 再生中インスタンス
		struct VoiceInstance {

			IXAudio2SourceVoice* voice = nullptr;

			// 再生中の情報
			float instanceVolume = 1.0f;
			bool loop = false;
		};

		//--------- variables ----------------------------------------------------

		static Audio* instance_;

		// マスター音量、全ての音に適用される値
		float masterVolume_ = 1.0f;

		// XAudio2
		ComPtr<IXAudio2> xAudio2_{};
		IXAudio2MasteringVoice* masteringVoice_ = nullptr;

		// 読み込んだサウンド
		std::unordered_map<std::string, SoundData> sounds_{};

		// 再生中の音リソース
		std::unordered_map<std::string, std::vector<VoiceInstance>> activeVoices_{};

		// 排他
		std::mutex mutex_;

		// COM、MFを自分で初期化したか
		bool mfStarted_ = false;

		//--------- functions ----------------------------------------------------

		// Sounds/配下のファイルを全て走査して読み込み
		void LoadAllSounds();

		// サウンドデータを読み込み
		void Load(const std::string& filename, AudioType type);
		// サウンドデータを解放
		void Unload();

		// 共通再生
		void PlayInternal(const std::string& name, bool loop, float volume);

		// 終了したVoiceを掃除
		void CleanupFinishedVoicesLocked(const std::string& key);

		// 全てのVoiceを掃除
		void CleanupAllFinishedVoicesLocked();

		// キー正規化
		std::string NormalizeKey(const std::string& nameOrPath) const;

		// ローダ
		SoundData LoadWaveFile(const std::string& filename);
		SoundData LoadMp3FileWithMediaFoundation(const std::string& filename);

		// そのvoiceに最終音量を適用
		void ApplyVoiceVolumeLocked(const std::string& key, VoiceInstance& inst);

		// 名前からサウンドデータを取得
		SoundData* FindSoundLocked(const std::string& key);

		// 拡張子から音源タイプを推測
		AudioType GuessAudioTypeFromPath(const std::filesystem::path& p) const;

		Audio() = default;
		~Audio() = default;
		Audio(const Audio&) = delete;
		Audio& operator=(const Audio&) = delete;
	};
}