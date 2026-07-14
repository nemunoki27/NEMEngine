#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleSizeOverLifetimeModule.h>

namespace Engine {

	//============================================================================
	//	ParticleTrailSizeOverLifetimeModule class
	//	軌跡点の寿命に応じてトレイルの幅を更新する
	//============================================================================
	class ParticleTrailSizeOverLifetimeModule :
		public ParticleSizeOverLifetimeModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleTrailSizeOverLifetimeModule();
		~ParticleTrailSizeOverLifetimeModule() override = default;

		ParticleModuleExecutionMode GetUpdateExecutionMode() const override { return ParticleModuleExecutionMode::None; }
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleTrailSizeOverLifetimeModule, "TrailSizeOverLifetime");
} // Engine
