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
	struct AudioSourceRuntimeData;

	//============================================================================
	//	AudioSourcePlayback class
	//	World内のAudioSource再生とVoiceの所有を管理する
	//============================================================================
	class AudioSourcePlayback {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// AudioSourceが所有する再生をすべて停止する
		void StopSourceVoices(AudioSourceRuntimeData& runtime);
		// AudioSourceへ再生インスタンスを追加する
		bool StartPlayback(Entity entity, const AudioSourceComponent& component, AudioSourceRuntimeData& runtime,
			AssetID clip, bool primary, bool loop, float volumeScale, AssetDatabase& database);
		// AudioSourceの再生要求を順番に処理する
		void ProcessCommands(Entity entity, const AudioSourceComponent& component,
			AudioSourceRuntimeData& runtime, AssetDatabase& database);
		// 再生中の音量とループ設定を反映する
		void UpdatePlaybackSettings( const AudioSourceComponent& component, AudioSourceRuntimeData& runtime);
		// 自然終了した再生インスタンスを除去する
		void CleanupFinishedPlaybacks(AudioSourceRuntimeData& runtime);
		// AudioSourceの公開用再生状態を更新する
		void RefreshRuntimeState(AudioSourceRuntimeData& runtime);
		// 所有Entityが無くなったVoiceを停止する
		void StopOrphanVoices(ECSWorld& world);
		// ワールド内のAudioSourceを停止する
		void StopAll(ECSWorld& world);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 再生中のVoiceと所有Entity
		std::unordered_map<uint64_t, Entity> runtimeVoices_;

		//--------- functions ----------------------------------------------------

		// 再生インスタンスを停止する
		void StopPlayback(AudioSourcePlaybackRuntime& playback);

	};
} // Engine
