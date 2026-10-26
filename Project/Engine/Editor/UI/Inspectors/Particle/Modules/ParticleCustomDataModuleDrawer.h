#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleCustomShaderParameterModuleDrawer.h"

namespace Engine {

	// 任意数値の編集を既存の数値カーブへ接続する
	class ParticleCustomDataModuleDrawer : public IParticleModuleDrawer {
	public:
		ParticleCustomDataModuleDrawer();
		bool Draw(IParticleModule& module) override;
	private:
		ParticleCustomShaderParameterModuleDrawer parameters_;
	};
}
