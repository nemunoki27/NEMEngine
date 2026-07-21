#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

	// AudioSourceへの再生要求
	enum class AudioSourceCommandType {

		Play,
		PlayOneShot,
		Pause,
		UnPause,
		Stop,
	};

	// AudioSourceSystemが順番に消費する再生要求
	struct AudioSourceCommand {

		AudioSourceCommandType type = AudioSourceCommandType::Play;
		AssetID clip{};
		float volumeScale = 1.0f;
		bool loop = false;
	};

	// AudioSourceが所有する再生インスタンス
	struct AudioSourcePlaybackRuntime {

		AssetID clip{};
		std::string key{};
		uint64_t voiceID = 0;
		float volumeScale = 1.0f;
		float appliedVolume = -1.0f;
		bool primary = false;
		bool loop = false;
		bool paused = false;
	};

	//============================================================================
	//	AudioSourceComponent struct
	//============================================================================
	// Entityに音声再生設定を持たせるコンポーネント
	struct AudioSourceComponent {

		// 再生する音声アセット
		AssetID clip{};
		// コンポーネントが有効か
		bool enabled = true;
		// Play開始時に自動再生するか
		bool playOnAwake = true;
		// ループ再生するか
		bool loop = false;
		// 音量
		float volume = 1.0f;

		// Runtime状態はSceneとPrefabに保存しない
		bool runtimePlaying = false;
		bool runtimePaused = false;
		bool runtimeActive = false;
		bool runtimePlayOnAwakeConsumed = false;
		std::vector<AudioSourcePlaybackRuntime> runtimePlaybacks{};
		std::vector<AudioSourceCommand> runtimeCommands{};

		// Clipを主再生として先頭から再生
		void Play();
		// Clipを重ねて一度だけ再生
		void PlayOneShot(AssetID audioClip, float volumeScale = 1.0f);
		// 所有する再生を一時停止
		void Pause();
		// 所有する一時停止中の再生を再開
		void UnPause();
		// 所有する再生をすべて停止
		void Stop();

		// 一時停止していない再生が存在するか
		bool IsPlaying() const { return runtimePlaying; }
	};

	// json変換
	void from_json(const nlohmann::json& in, AudioSourceComponent& component);
	void to_json(nlohmann::json& out, const AudioSourceComponent& component);

	ENGINE_REGISTER_COMPONENT(AudioSourceComponent, "AudioSource");
} // Engine
