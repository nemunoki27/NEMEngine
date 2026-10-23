#pragma once

#include "IParticleModuleDrawer.h"
#include "ParticleFloatAnimationDrawer.h"

namespace Engine {

	//============================================================================
	//	ParticleSpiralMovementModuleDrawer class
	//	軌道設定と寿命カーブの編集表示
	//============================================================================
	class ParticleSpiralMovementModuleDrawer : public IParticleModuleDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		bool Draw(IParticleModule& module) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		ParticleFloatAnimationEditState radiusState_{};
		ParticleFloatAnimationEditState turnsState_{};
	};
}
