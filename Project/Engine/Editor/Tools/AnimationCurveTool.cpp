#include "AnimationCurveTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/ImGui/MyGUI.h>

// imgui
#include <imgui.h>

//============================================================================
//	AnimationCurveTool classMethods
//============================================================================

Engine::AnimationCurveTool::AnimationCurveTool() {

	SetupSampleCurve();
}

void Engine::AnimationCurveTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::AnimationCurveTool::DrawEditorTool([[maybe_unused]] const EditorToolContext& context) {

	if (!openWindow_) {
		return;
	}
	DrawWindow(context);
}

void Engine::AnimationCurveTool::DrawWindow([[maybe_unused]] const EditorToolContext& context) {

	if (!ImGui::Begin("Animation Curve", &openWindow_)) {
		ImGui::End();
		return;
	}

	ImGui::TextDisabled("Curve asset editor base");
	ImGui::Separator();

	if (ImGui::RadioButton("Transform", !showColorCurve_)) {
		showColorCurve_ = false;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Color", showColorCurve_)) {
		showColorCurve_ = true;
	}

	if (showColorCurve_) {

		MyGUI::CurveEditor("ColorCurve", colorCurve_, colorState_, CurveEditSetting{ .autoFit = true });
	} else {
		MyGUI::CurveEditor("TransformCurve", transformCurve_, transformState_, CurveEditSetting{ .autoFit = true });
	}

	ImGui::End();
}

void Engine::AnimationCurveTool::SetupSampleCurve() {

	if (!transformCurve_.channels[0].keys.empty()) {
		return;
	}

	transformCurve_.channels[0].AddKey(0.0f, 0.0f, CurveInterpolationMode::Linear);
	transformCurve_.channels[0].AddKey(0.5f, 1.0f, CurveInterpolationMode::Cubic);
	transformCurve_.channels[0].AddKey(1.0f, 0.0f, CurveInterpolationMode::Linear);
	transformCurve_.channels[1].AddKey(0.0f, 0.0f, CurveInterpolationMode::Linear);
	transformCurve_.channels[1].AddKey(1.0f, 0.5f, CurveInterpolationMode::Linear);
	transformCurve_.channels[2].AddKey(0.0f, 0.0f, CurveInterpolationMode::Linear);
	transformCurve_.channels[2].AddKey(1.0f, 1.0f, CurveInterpolationMode::Linear);

	colorCurve_.channels[0].AddKey(0.0f, 1.0f, CurveInterpolationMode::Linear);
	colorCurve_.channels[0].AddKey(1.0f, 0.2f, CurveInterpolationMode::Linear);
	colorCurve_.channels[1].AddKey(0.0f, 0.2f, CurveInterpolationMode::Linear);
	colorCurve_.channels[1].AddKey(1.0f, 1.0f, CurveInterpolationMode::Linear);
	colorCurve_.channels[2].AddKey(0.0f, 0.2f, CurveInterpolationMode::Linear);
	colorCurve_.channels[2].AddKey(1.0f, 0.4f, CurveInterpolationMode::Linear);
	colorCurve_.channels[3].AddKey(0.0f, 1.0f, CurveInterpolationMode::Constant);
	colorCurve_.channels[3].AddKey(1.0f, 1.0f, CurveInterpolationMode::Constant);

	transformState_.frameSelectionRequest = true;
	colorState_.visibleValueMin = 0.0f;
	colorState_.visibleValueMax = 1.0f;
	colorState_.frameSelectionRequest = true;
}