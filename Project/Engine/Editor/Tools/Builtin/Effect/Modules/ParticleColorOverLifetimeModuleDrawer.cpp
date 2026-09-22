#include "ParticleColorOverLifetimeModuleDrawer.h"

#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleColorOverLifetimeModule.h>
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

using namespace Engine;

bool ParticleColorOverLifetimeModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleColorOverLifetimeModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto settings = concrete->GetSettings();

	bool changed = false;
	changed |= MyGUI::ColorEdit("開始色", settings.startColor).valueChanged;
	changed |= MyGUI::ColorEdit("終了色", settings.endColor).valueChanged;
	changed |= ParticleGUI::DrawInterpolationEasing(settings.easingType);
	changed |= MyGUI::Checkbox("カーブを使用", settings.useCurve);
	if (settings.useCurve) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		// 進行度のカーブなので時間軸を0~1で固定する
		setting.fixedTimeRange = true;
		changed |= MyGUI::CurveEditor("ColorCurve", settings.curve, curveState_, setting).valueChanged;
		// 可視時間範囲の色遷移を帯で表示する
		MyGUI::CurveColorGradientBar(settings.curve.channels, curveState_.visibleTimeMin, curveState_.visibleTimeMax, true);

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = {
				{ "RGB", { 0u, 1u, 2u } }, { "R", { 0u } }, { "G", { 1u } }, { "B", { 2u } }, { "A", { 3u } } };
			if (DrawCurveGenerator(generatorState_, settings.curve.channels, targets)) {
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
