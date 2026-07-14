#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorOverLifetimeModule.h>

namespace Engine {

	//============================================================================
	//	ParticleTrailColorOverLifetimeModule class
	//	軌跡点の寿命に応じてトレイルの色を更新する
	//============================================================================
	class ParticleTrailColorOverLifetimeModule :
		public ParticleColorOverLifetimeModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleTrailColorOverLifetimeModule() = default;
		~ParticleTrailColorOverLifetimeModule() override = default;

		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::None; }
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleTrailColorOverLifetimeModule, "TrailColorOverLifetime");
} // Engine
