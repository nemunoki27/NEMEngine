#include "ParticlePendulumMovementModuleDrawer.h"

#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticlePendulumMovementModule.h>
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

using namespace Engine;

bool ParticlePendulumMovementModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticlePendulumMovementModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto settings = concrete->GetSettings();

	bool changed = false;
	changed |= ParticleFloatAnimationDrawer::Draw(
		"振り子の長さ", "Length", "開始長さ", "終了長さ", settings.length, lengthState_, 0.0f, 10000.0f);
	changed |= ParticleFloatAnimationDrawer::Draw(
		"最大振れ角", "MaxAngle", "開始角度", "終了角度", settings.maxAngle, maxAngleState_, 0.0f, 180.0f);
	changed |= ParticleFloatAnimationDrawer::Draw(
		"振動回数", "Cycles", "開始回数", "終了回数", settings.cycles, cyclesState_, 0.0f, 1000.0f);

	if (MyGUI::CollapsingHeader("軌道", false)) {

		changed |= MyGUI::DragFloat(
			"振り子平面角度", settings.planeAngle,
			ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f)).valueChanged;
		changed |= MyGUI::DragFloat(
			"開始位相", settings.startPhase,
			ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f)).valueChanged;
		changed |= MyGUI::DragFloat(
			"粒子ごとの位相差", settings.particlePhaseOffset,
			ParticleGUI::MakeDragSetting(-360.0f, 360.0f)).valueChanged;
		changed |= MyGUI::DragFloat(
			"円弧の強さ", settings.arcStrength,
			ParticleGUI::MakeDragSetting(-1.0f, 1.0f)).valueChanged;
		changed |= MyGUI::Checkbox("逆方向", settings.reverse);
	}
	if (changed) {
		concrete->SetSettings(settings);
	}
	return changed;

}
