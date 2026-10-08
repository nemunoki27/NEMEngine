#pragma once

#include "IParticleModuleDrawer.h"
#include "ParticleFloatAnimationDrawer.h"

namespace Engine {

	//============================================================================
	//	ParticlePendulumMovementModuleDrawer class
	//	軌道設定と寿命カーブの編集表示
	//============================================================================
	class ParticlePendulumMovementModuleDrawer : public IParticleModuleDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		bool Draw(IParticleModule& module) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		ParticleFloatAnimationEditState lengthState_{};
		ParticleFloatAnimationEditState maxAngleState_{};
		ParticleFloatAnimationEditState cyclesState_{};
	};
}
