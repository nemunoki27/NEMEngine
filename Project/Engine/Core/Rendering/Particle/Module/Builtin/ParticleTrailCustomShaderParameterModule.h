#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCustomShaderParameterModule.h>

namespace Engine {

	//============================================================================
	//	ParticleTrailCustomShaderParameterModule class
	//	軌跡点の寿命に応じてトレイルのシェーダー固有値を更新する
	//============================================================================
	class ParticleTrailCustomShaderParameterModule :
		public ParticleCustomShaderParameterModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleTrailCustomShaderParameterModule() = default;
		~ParticleTrailCustomShaderParameterModule() override = default;

	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleTrailCustomShaderParameterModule, "TrailCustomShaderParameter");
} // Engine
