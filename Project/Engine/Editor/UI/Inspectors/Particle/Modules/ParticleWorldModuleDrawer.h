#pragma once

//============================================================================
//	include
//============================================================================
#include "IParticleModuleDrawer.h"

namespace Engine {

	class ParticleWorldModuleDrawer : public IParticleModuleDrawer {
	public:

		bool Draw(IParticleModule& module) override;
		bool Draw(IParticleModule& module, const EditorPanelContext& context) override;
	};
}
