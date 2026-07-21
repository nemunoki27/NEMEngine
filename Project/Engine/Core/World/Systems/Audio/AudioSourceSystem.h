#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <cstdint>
#include <unordered_map>

namespace Engine {
	class AssetDatabase;
	struct AudioSourceComponent;
	struct AudioSourcePlaybackRuntime;

	//============================================================================
	//	AudioSourceSystem class
	//	AudioSourceComponentの再生状態を管理するシステム
	//============================================================================
	class AudioSourceSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AudioSourceSystem() = default;
		~AudioSourceSystem() = default;

		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		void Update(ECSWorld& world, SystemContext& context) override;
		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		const char* GetName() const override { return "AudioSourceSystem"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 再生中のVoiceと所有Entity
		std::unordered_map<uint64_t, Entity> runtimeVoices_;

		//--------- functions ----------------------------------------------------

		// 再生インスタンスを停止する
		void StopPlayback(AudioSourcePlaybackRuntime& playback);
		// AudioSourceが所有する再生をすべて停止する
		void StopSourceVoices(AudioSourceComponent& component);
		// AudioSourceへ再生インスタンスを追加する
		bool StartPlayback(Entity entity, AudioSourceComponent& component,
			AssetID clip, bool primary, bool loop, float volumeScale, AssetDatabase& database);
		// AudioSourceの再生要求を順番に処理する
		void ProcessCommands(Entity entity, AudioSourceComponent& component, AssetDatabase& database);
		// 再生中の音量とループ設定を反映する
		void UpdatePlaybackSettings(AudioSourceComponent& component);
		// 自然終了した再生インスタンスを除去する
		void CleanupFinishedPlaybacks(AudioSourceComponent& component);
		// AudioSourceの公開用再生状態を更新する
		void RefreshRuntimeState(AudioSourceComponent& component);
		// 所有Entityが無くなったVoiceを停止する
		void StopOrphanVoices(ECSWorld& world);
		// ワールド内のAudioSourceを停止する
		void StopAll(ECSWorld& world);
	};
} // Engine

