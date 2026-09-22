#include "ParticleGravityForceModuleDrawer.h"

#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleGravityForceModule.h>
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

using namespace Engine;

bool ParticleGravityForceModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleGravityForceModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto settings = concrete->GetSettings();

	bool changed = false;
	changed |= MyGUI::DragVector3("重力", settings.gravity, ParticleGUI::MakeDragSetting(-1000.0f, 1000.0f)).valueChanged;
	changed |= MyGUI::Checkbox("地面で反射", settings.reflectGround);
	if (settings.reflectGround) {

		changed |= MyGUI::DragFloat("地面の高さ", settings.reflectGroundY, ParticleGUI::MakeDragSetting(-10000.0f, 10000.0f)).valueChanged;
		changed |= MyGUI::DragFloat("反発係数", settings.restitution, ParticleGUI::MakeDragSetting(0.0f, 1.0f, 0.005f)).valueChanged;
	}
	if (changed) {
		concrete->SetSettings(settings);
	}
	return changed;
}
