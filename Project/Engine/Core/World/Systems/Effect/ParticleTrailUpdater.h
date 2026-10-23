#pragma once

//============================================================================
// include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

namespace Engine {

	//============================================================================
	// ParticleTrailUpdater class
	// 粒子軌跡と発生順Ribbonの点列を更新する
	//============================================================================
	class ParticleTrailUpdater {
	public:
		//========================================================================
		// public Methods
		//========================================================================

		// 点の寿命を進め、空き領域へ記録する
		void RecordTrails(ParticleEmitterRuntime& state, const ParticleTrailSettings& settings, float deltaTime);
	private:
		//========================================================================
		// private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<Particle> ribbonParticles_;
		std::vector<ParticleTrailHandle> expired_;

		//--------- functions ----------------------------------------------------

		// 生存粒子を発生順に接続する
		void RecordRibbon(ParticleEmitterRuntime& state);
	};
}
