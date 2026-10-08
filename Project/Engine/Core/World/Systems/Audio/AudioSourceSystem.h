#pragma once

//============================================================================
//	include
//============================================================================
#include "AudioSourcePlayback.h"
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
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AudioSourcePlayback playback_;
	};
}
