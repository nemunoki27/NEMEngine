#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/System/Interface/ISystem.h>

namespace Engine {

	//============================================================================
	//	AudioSourceSystem class
	//	AudioSourceComponentの再生状態を管理するシステム
	//============================================================================
	class AudioSourceSystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AudioSourceSystem() = default;
		~AudioSourceSystem() = default;

		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		void Update(ECSWorld& world, SystemContext& context) override;

		const char* GetName() const override { return "AudioSourceSystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// ワールド内のAudioSourceを停止する
		void StopAll(ECSWorld& world);
	};
} // Engine
