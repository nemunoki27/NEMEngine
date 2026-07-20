#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

// c++
#include <cstdint>
#include <unordered_map>

namespace Engine {

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

		// 所有Entityが無くなったVoiceを停止する
		void StopOrphanVoices(ECSWorld& world);
		// ワールド内のAudioSourceを停止する
		void StopAll(ECSWorld& world);
	};
} // Engine

