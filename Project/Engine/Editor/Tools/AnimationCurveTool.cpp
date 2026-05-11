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

	if (!ImGui::Begin("AnimationClip Curve", &openWindow_)) {
		ImGui::End();
		return;
	}

	ImGui::TextDisabled("Curve asset editor base");
	ImGui::Separator();

	if (ImGui::RadioButton("Transform", sampleType_ == AnimationCurveToolSampleType::Transform)) {
		sampleType_ = AnimationCurveToolSampleType::Transform;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Color", sampleType_ == AnimationCurveToolSampleType::Color)) {
		sampleType_ = AnimationCurveToolSampleType::Color;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("Quaternion", sampleType_ == AnimationCurveToolSampleType::Quaternion)) {
		sampleType_ = AnimationCurveToolSampleType::Quaternion;
	}

	if (sampleType_ == AnimationCurveToolSampleType::Color) {

		MyGUI::CurveEditor("ColorCurve", colorCurve_, colorState_, CurveEditSetting{ .autoFit = true });
	} else if (sampleType_ == AnimationCurveToolSampleType::Quaternion) {
		MyGUI::CurveEditor("QuaternionCurve", quaternionCurve_, quaternionState_, CurveEditSetting{ .autoFit = true });
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

	// Axisは離散値として扱うため、補間で中間軸が出ないようConstantに固定する。
	quaternionCurve_.channels[0].AddKey(0.0f, 0.0f, CurveInterpolationMode::Constant);
	quaternionCurve_.channels[0].AddKey(0.5f, 1.0f, CurveInterpolationMode::Constant);
	quaternionCurve_.channels[0].AddKey(1.0f, 2.0f, CurveInterpolationMode::Constant);
	quaternionCurve_.channels[1].AddKey(0.0f, 0.0f, CurveInterpolationMode::Linear);
	quaternionCurve_.channels[1].AddKey(0.5f, 90.0f, CurveInterpolationMode::Linear);
	quaternionCurve_.channels[1].AddKey(1.0f, 180.0f, CurveInterpolationMode::Linear);
	quaternionCurve_.axisKeys.resize(3);
	quaternionCurve_.axisKeys[0].axes = { Axis::X };
	quaternionCurve_.axisKeys[1].axes = { Axis::X, Axis::Y };
	quaternionCurve_.axisKeys[2].useCustomAxis = true;
	quaternionCurve_.axisKeys[2].customAxis = Vector3(0.0f, 1.0f, 1.0f);

	transformState_.frameSelectionRequest = true;
	colorState_.visibleValueMin = 0.0f;
	colorState_.visibleValueMax = 1.0f;
	colorState_.frameSelectionRequest = true;
	quaternionState_.visibleValueMin = 0.0f;
	quaternionState_.visibleValueMax = 180.0f;
	quaternionState_.frameSelectionRequest = true;
}
