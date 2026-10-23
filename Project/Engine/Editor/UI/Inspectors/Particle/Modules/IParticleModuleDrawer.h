#pragma once

#include <Engine/Core/Rendering/Particle/Module/Base/IParticleModule.h>

namespace Engine {

	//============================================================================
	//	IParticleModuleDrawer class
	//	モジュールの編集表示と編集状態を所有する
	//============================================================================
	class IParticleModuleDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		virtual ~IParticleModuleDrawer() = default;
		virtual bool Draw(IParticleModule& module) = 0;
	};
}
