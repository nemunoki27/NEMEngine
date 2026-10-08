#include "ParticleNoiseForceModuleDrawer.h"

#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleNoiseForceModule.h>
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

using namespace Engine;

bool ParticleNoiseForceModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleNoiseForceModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto settings = concrete->GetSettings();

	bool changed = false;
	changed |= MyGUI::DragFloat("強さ", settings.strength, ParticleGUI::MakeDragSetting(0.0f, 1000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("周波数", settings.frequency, ParticleGUI::MakeDragSetting(0.001f, 100.0f)).valueChanged;
	if (changed) {
		concrete->SetSettings(settings);
	}
	return changed;
}
