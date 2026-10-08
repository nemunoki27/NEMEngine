#pragma once

#include "IParticleModuleDrawer.h"

namespace Engine {

	//============================================================================
	//	ParticleNoiseUVModuleDrawer class
	//	モジュールの設定を編集し変更時に実行定義へ戻す
	//============================================================================
	class ParticleNoiseUVModuleDrawer : public IParticleModuleDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		bool Draw(IParticleModule& module) override;
	};
}
