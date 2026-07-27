#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
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

	// チャンク外で所有するAudioSourceの実行時データ
	struct AudioSourceRuntimeData {

		bool playing = false;
		bool paused = false;
		bool active = false;
		bool playOnAwakeConsumed = false;
		std::vector<AudioSourcePlaybackRuntime> playbacks{};
		std::vector<AudioSourceCommand> commands{};
	};

	struct AudioSourceRuntimeStorageTag;
	using AudioSourceRuntimeStorage =
		GenerationalPool<AudioSourceRuntimeData, AudioSourceRuntimeStorageTag>;
	using AudioSourceRuntimeHandle =
		AudioSourceRuntimeStorage::Handle;

	// ECSチャンクには世代付きハンドルだけを保持する
	struct AudioSourceRuntimeComponent {

		static constexpr bool kSerializable = false;
		static constexpr bool kHasECSHooks = true;

		AudioSourceRuntimeHandle handle{};

		static void OnAdded(
			ECSWorld& world, const Entity& entity, AudioSourceRuntimeComponent& component);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, AudioSourceRuntimeComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, AudioSourceRuntimeComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, AudioSourceRuntimeComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const AudioSourceRuntimeComponent& component, nlohmann::json& out);
	};

	//============================================================================
	//	AudioSourceComponent struct
	//============================================================================
	// Entityに音声再生設定を持たせるコンポーネント
	struct AudioSourceComponent {

		static constexpr bool kHasECSHooks = true;

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

		// Registryから呼ばれるRuntime状態のライフサイクル
		static void OnAdded(
			ECSWorld& world, const Entity& entity, AudioSourceComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, AudioSourceComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, AudioSourceComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, AudioSourceComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const AudioSourceComponent& component, nlohmann::json& out);
	};

	// AudioSourceのRuntimeデータを返す
	AudioSourceRuntimeData* TryGetAudioSourceRuntime(
		ECSWorld& world, const Entity& entity);
	const AudioSourceRuntimeData* TryGetAudioSourceRuntime(
		const ECSWorld& world, const Entity& entity);
	// 再生操作を次のAudioSourceSystem更新へ積む
	void RequestAudioPlay(ECSWorld& world, const Entity& entity);
	void RequestAudioPlayOneShot(ECSWorld& world, const Entity& entity,
		AssetID clip, float volumeScale = 1.0f);
	void RequestAudioPause(ECSWorld& world, const Entity& entity);
	void RequestAudioUnPause(ECSWorld& world, const Entity& entity);
	void RequestAudioStop(ECSWorld& world, const Entity& entity);
	bool IsAudioSourcePlaying(const ECSWorld& world, const Entity& entity);

	// json変換
	void from_json(const nlohmann::json& in, AudioSourceComponent& component);
	void to_json(nlohmann::json& out, const AudioSourceComponent& component);

} // Engine
