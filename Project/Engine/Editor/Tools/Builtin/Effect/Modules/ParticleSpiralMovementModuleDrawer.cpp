#include "ParticleSpiralMovementModuleDrawer.h"

#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleSpiralMovementModule.h>
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

using namespace Engine;

bool ParticleSpiralMovementModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleSpiralMovementModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto settings = concrete->GetSettings();

	bool changed = false;
	changed |= ParticleFloatAnimationDrawer::Draw(
		"渦半径", "Radius", "開始半径", "終了半径", settings.radius, radiusState_, 0.0f, 10000.0f);
	changed |= ParticleFloatAnimationDrawer::Draw(
		"渦巻き回数", "Turns", "開始回転数", "終了回転数", settings.turns, turnsState_, 0.0f, 1000.0f);

	if (MyGUI::CollapsingHeader("角度", false)) {

		changed |= MyGUI::DragFloat(
			"開始角度", settings.startAngle, ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f)).valueChanged;
		changed |= MyGUI::DragFloat(
			"粒子ごとの角度差", settings.particleAngleOffset,
			ParticleGUI::MakeDragSetting(-360.0f, 360.0f)).valueChanged;
		changed |= MyGUI::Checkbox("逆回転", settings.reverse);
	}
	if (changed) {
		concrete->SetSettings(settings);
	}
	return changed;

}
