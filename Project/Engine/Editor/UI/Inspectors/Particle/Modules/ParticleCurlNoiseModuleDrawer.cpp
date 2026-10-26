#include "ParticleCurlNoiseModuleDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleCurlNoiseModule.h>
#include <Engine/Editor/UI/Inspectors/Particle/GUI/ParticleGUIHelpers.h>

using namespace Engine;

bool ParticleCurlNoiseModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleCurlNoiseModule*>(&module);
	if (!concrete) return false;
	auto settings = concrete->GetSettings();
	bool changed = false;
	changed |= MyGUI::DragFloat("強さ", settings.strength, ParticleGUI::MakeDragSetting(0.0f, 1000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("周波数", settings.frequency, ParticleGUI::MakeDragSetting(0.001f, 100.0f)).valueChanged;
	changed |= MyGUI::DragVector3("流れの移動速度", settings.scrollSpeed).valueChanged;
	if (changed) concrete->SetSettings(settings);
	return changed;
}
