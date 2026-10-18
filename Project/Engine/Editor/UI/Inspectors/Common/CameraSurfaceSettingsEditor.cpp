#include "CameraSurfaceSettingsEditor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>

// c++
#include <algorithm>
#include <array>

namespace {

	void Accumulate(Engine::ValueEditResult& result, const Engine::ValueEditResult& field) {

		result.valueChanged |= field.valueChanged;
		result.anyItemActive |= field.anyItemActive;
		result.editFinished |= field.editFinished;
	}

	void AccumulateCheckbox(Engine::ValueEditResult& result, const char* label, bool& value) {

		const bool changed = Engine::MyGUI::Checkbox(label, value);
		result.valueChanged |= changed;
		result.editFinished |= changed;
	}

	Engine::ValueEditResult DrawQuality(int32_t& quality) {

		const std::array<std::string, 3> labels{ "低", "標準", "高" };
		std::string selected = labels[static_cast<size_t>(std::clamp(quality, 0, 2))];
		const auto result = Engine::MyGUI::StringCombo("品質", selected, labels);
		if (result.valueChanged) quality = static_cast<int32_t>(std::find(labels.begin(), labels.end(), selected) - labels.begin());
		return result;
	}
}

//============================================================================
//	CameraSurfaceSettingsEditor classMethods
//============================================================================
Engine::ValueEditResult Engine::CameraSurfaceSettingsEditor::DrawReflection(CameraCommon& settings) {

	ValueEditResult result{};
	AccumulateCheckbox(result, "反射を使用", settings.reflectionEnabled);
	Accumulate(result, MyGUI::DragFloat("反射強度", settings.reflectionIntensity,
		{ .dragSpeed = 0.01f, .minValue = 0.0f }));
	Accumulate(result, MyGUI::DragFloat("最大距離", settings.reflectionMaxDistance,
		{ .dragSpeed = 0.1f, .minValue = 0.01f, .maxValue = 100000.0f }));
	Accumulate(result, DrawQuality(settings.reflectionQuality));
	// 保存済みの調整値もCameraから編集する
	if (settings.reflectionParameters.contains("passes")) {
		for (auto& pass : settings.reflectionParameters["passes"]) {
			ImGui::PushID(pass.value("id", std::string{}).c_str());
			for (auto& parameter : pass["parameters"]) {
				if (!parameter["value"].is_number()) continue;
				float value = parameter["value"].get<float>();
				const auto field = MyGUI::DragFloat(parameter.value("name", std::string{}).c_str(), value);
				Accumulate(result, field);
				if (field.valueChanged) parameter["value"] = value;
			}
			ImGui::PopID();
		}
	}
	return result;
}

Engine::ValueEditResult Engine::CameraSurfaceSettingsEditor::DrawAmbientOcclusion(
	CameraCommon& settings, GraphicsCore* graphicsCore) {

	ValueEditResult result{};
	const bool supported = graphicsCore && graphicsCore->GetDXObject().GetFeatureController().GetSupport().SupportsInlineRayTracingPath() &&
		graphicsCore->GetDXObject().GetFeatureController().GetSupport().SupportsShaderModel6_6();
	ImGui::BeginDisabled(!supported);
	AccumulateCheckbox(result, "AOを使用", settings.ambientOcclusionEnabled);
	Accumulate(result, MyGUI::DragFloat("強度", settings.ambientOcclusionIntensity,
		{ .dragSpeed = 0.01f, .minValue = 0.0f }));
	Accumulate(result, MyGUI::DragFloat("半径", settings.ambientOcclusionRadius,
		{ .dragSpeed = 0.01f, .minValue = 0.001f, .maxValue = 1000.0f }));
	Accumulate(result, DrawQuality(settings.ambientOcclusionQuality));
	ImGui::EndDisabled();
	if (!supported) ImGui::TextDisabled("このGPUではレイトレーシングAOを使用できません");
	return result;
}
