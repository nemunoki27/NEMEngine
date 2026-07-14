#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorUVModule.h>

namespace Engine {

	//============================================================================
	//	ParticleTrailColorUVModule class
	//	軌跡点の寿命に応じてトレイルのUV行列を更新する
	//============================================================================
	class ParticleTrailColorUVModule :
		public ParticleColorUVModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleTrailColorUVModule() = default;
		~ParticleTrailColorUVModule() override = default;

		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::None; }
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleTrailColorUVModule, "TrailColorUV");
} // Engine
