#include "ParticleScaleOverLifetimeModuleDrawer.h"

#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleScaleOverLifetimeModule.h>
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

using namespace Engine;

bool ParticleScaleOverLifetimeModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleScaleOverLifetimeModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto settings = concrete->GetSettings();

	bool changed = false;
	changed |= MyGUI::DragVector3("開始スケール", settings.startScale, ParticleGUI::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= MyGUI::DragVector3("終了スケール", settings.endScale, ParticleGUI::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= ParticleGUI::DrawInterpolationEasing(settings.easingType);
	changed |= MyGUI::Checkbox("カーブを使用", settings.useCurve);
	if (settings.useCurve) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		// 進行度のカーブなので時間軸を0~1で固定する
		setting.fixedTimeRange = true;
		changed |= MyGUI::CurveEditor("ScaleCurve", settings.curve, curveState_, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = {
				{ "XYZ", { 0u, 1u, 2u } }, { "X", { 0u } }, { "Y", { 1u } }, { "Z", { 2u } } };
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
