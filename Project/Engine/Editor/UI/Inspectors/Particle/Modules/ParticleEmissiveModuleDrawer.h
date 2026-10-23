#pragma once

#include "IParticleModuleDrawer.h"

namespace Engine {

	//============================================================================
	//	ParticleEmissiveModuleDrawer class
	//	モジュールの設定を編集し変更時に実行定義へ戻す
	//============================================================================
	class ParticleEmissiveModuleDrawer : public IParticleModuleDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		bool Draw(IParticleModule& module) override;
	};
}
