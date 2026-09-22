#include "ParticleEmissiveModuleDrawer.h"

#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleEmissiveModule.h>
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

using namespace Engine;

bool ParticleEmissiveModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleEmissiveModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto settings = concrete->GetSettings();

	bool changed = false;
	ImGui::PushID("Color");
	changed |= MyGUI::ColorEdit("開始色", settings.startColor).valueChanged;
	changed |= MyGUI::ColorEdit("終了色", settings.endColor).valueChanged;
	changed |= ParticleGUI::DrawInterpolationEasing(settings.colorEasingType);
	ImGui::PopID();

	ImGui::PushID("Intensity");
	changed |= MyGUI::DragFloat("開始強度", settings.startIntensity, ParticleGUI::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= MyGUI::DragFloat("終了強度", settings.endIntensity, ParticleGUI::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= ParticleGUI::DrawInterpolationEasing(settings.intensityEasingType);
	ImGui::PopID();
	if (changed) {
		concrete->SetSettings(settings);
	}
	return changed;
}
