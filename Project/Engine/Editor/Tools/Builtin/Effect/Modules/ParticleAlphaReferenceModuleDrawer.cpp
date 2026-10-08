#include "ParticleAlphaReferenceModuleDrawer.h"

#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleAlphaReferenceModule.h>
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

using namespace Engine;

bool ParticleAlphaReferenceModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleAlphaReferenceModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto settings = concrete->GetSettings();

	bool changed = false;
	changed |= MyGUI::DragFloat("開始閾値", settings.startReference, ParticleGUI::MakeDragSetting(0.0f, 1.0f, 0.005f)).valueChanged;
	changed |= MyGUI::DragFloat("終了閾値", settings.endReference, ParticleGUI::MakeDragSetting(0.0f, 1.0f, 0.005f)).valueChanged;
	changed |= ParticleGUI::DrawInterpolationEasing(settings.easingType);
	if (changed) {
		concrete->SetSettings(settings);
	}
	return changed;
}
