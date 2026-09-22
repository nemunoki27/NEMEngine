#include "ParticleSizeOverLifetimeModuleDrawer.h"

#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleSizeOverLifetimeModule.h>
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

using namespace Engine;

bool ParticleSizeOverLifetimeModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleSizeOverLifetimeModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto settings = concrete->GetSettings();

	bool changed = false;
	changed |= MyGUI::DragFloat("開始倍率", settings.startScale, ParticleGUI::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= MyGUI::DragFloat("終了倍率", settings.endScale, ParticleGUI::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= ParticleGUI::DrawInterpolationEasing(settings.easingType);
	changed |= MyGUI::Checkbox("カーブを使用", settings.useCurve);
	if (settings.useCurve) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		// 進行度のカーブなので時間軸を0~1で固定する
		setting.fixedTimeRange = true;
		changed |= MyGUI::CurveEditor("SizeCurve", settings.curve, curveState_, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = { { "値", { 0u } } };
			if (DrawCurveGenerator(generatorState_, std::span<CurveChannel>(&settings.curve.channel, 1), targets)) {
				changed = true;
			}
		}
	}
	changed |= ParticleGUI::DrawLoopSettings(settings.loop);
	if (changed) {
		concrete->SetSettings(settings);
	}
	return changed;
}
