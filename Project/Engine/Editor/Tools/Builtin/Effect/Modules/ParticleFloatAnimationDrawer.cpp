#include "ParticleFloatAnimationDrawer.h"

#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

bool Engine::ParticleFloatAnimationDrawer::Draw(
	const char* header, const char* id, const char* startLabel, const char* endLabel,
	ParticleFloatAnimationSettings& settings, ParticleFloatAnimationEditState& state, float minValue, float maxValue) {

	if (!MyGUI::CollapsingHeader(header, false)) {
		return false;
	}

	ImGui::PushID(id);
	bool changed = false;
	changed |= MyGUI::DragFloat(
		startLabel, settings.start, ParticleGUI::MakeDragSetting(minValue, maxValue)).valueChanged;
	changed |= MyGUI::DragFloat(
		endLabel, settings.end, ParticleGUI::MakeDragSetting(minValue, maxValue)).valueChanged;
	changed |= ParticleGUI::DrawInterpolationEasing(settings.easingType);
	changed |= MyGUI::Checkbox("カーブを使用", settings.useCurve);
	if (settings.useCurve) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		changed |= MyGUI::CurveEditor(
			"Curve", settings.curve, state.curveState, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = { { "値", { 0u } } };
			changed |= DrawCurveGenerator(
				state.generatorState, GetCurveChannels(settings.curve), targets);
		}
	}
	changed |= ParticleGUI::DrawLoopSettings(settings.loop);
	ImGui::PopID();
	return changed;
}
