#include "ParticleShapeOverLifetimeModuleDrawer.h"

#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleShapeOverLifetimeModule.h>
#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

#include <span>

using namespace Engine::ParticleShapeAnimation;

bool Engine::ParticleShapeOverLifetimeModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleShapeOverLifetimeModule*>(&module);
	if (!concrete) {
		return false;
	}
	settings_ = concrete->GetSettings();
	parametersInserted_ = false;

	bool changed = false;
	ParticleParametricShapeRegistry& registry = ParticleParametricShapeRegistry::GetInstance();
	const std::vector<PrimitiveType> types = registry.GetTypes();

	int32_t currentIndex = 0;
	bool found = false;
	for (int32_t i = 0; i < static_cast<int32_t>(types.size()); ++i) {
		if (types[i] == settings_.shape) { currentIndex = i; found = true; break; }
	}
	if (!found && !types.empty()) {

		settings_.shape = types.front();
		ParticleShapeAnimation::EnsureParameters(settings_);
		changed = true;
	}

	if (!types.empty() && MyGUI::BeginPropertyRow("対象形状")) {

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (ImGui::BeginCombo("##Value", EnumAdapter<PrimitiveType>::ToString(types[currentIndex]))) {
			for (int32_t i = 0; i < static_cast<int32_t>(types.size()); ++i) {
				if (ImGui::Selectable(EnumAdapter<PrimitiveType>::ToString(types[i]), i == currentIndex)) {

					settings_.shape = types[i];
					ParticleShapeAnimation::EnsureParameters(settings_);
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		MyGUI::EndPropertyRow();
	}

	if (settings_.shape == PrimitiveType::Ring) {

		changed |= DrawParameter("外周半径", kRingOuterRadius, 1.0f, 0.0f, 10000.0f);
		changed |= DrawParameter("内周半径", kRingInnerRadius, 0.5f, 0.0f, 10000.0f);
		changed |= DrawParameter("開始角", kRingStartAngle, 0.0f, 0.0f, 360.0f, 0.5f);
		changed |= DrawParameter("終了角", kRingEndAngle, 360.0f, 0.0f, 360.0f, 0.5f);
	} else if (settings_.shape == PrimitiveType::Cylinder) {

		changed |= DrawParameter("上面半径", kCylinderTopRadius, 1.0f, 0.0f, 10000.0f);
		changed |= DrawParameter("中心半径", kCylinderCenterRadius, 1.0f, 0.0f, 10000.0f);
		changed |= DrawParameter("下面半径", kCylinderBottomRadius, 1.0f, 0.0f, 10000.0f);
		changed |= DrawParameter("上面Weight", kCylinderTopRadiusWeight, 0.0f, 0.0f, 1.0f);
		changed |= DrawParameter("下面Weight", kCylinderBottomRadiusWeight, 0.0f, 0.0f, 1.0f);
		changed |= DrawColorParameter("上面色", kCylinderTopColor, Color4::White());
		changed |= DrawColorParameter("中心色", kCylinderCenterColor, Color4::White());
		changed |= DrawColorParameter("底面色", kCylinderBottomColor, Color4::White());
		changed |= DrawParameter("高さ", kCylinderHeight, 2.0f, 0.0f, 10000.0f);
		changed |= DrawParameter("展開角", kCylinderMaxAngle, 360.0f, 0.0f, 360.0f, 0.5f);
	}
	if (changed || parametersInserted_) {
		concrete->SetSettings(settings_);
	}
	return changed;

}

bool Engine::ParticleShapeOverLifetimeModuleDrawer::DrawParameter(
	const char* label, const char* key, float defaultValue,
	float minValue, float maxValue, float dragSpeed) {

	auto [it, inserted] = settings_.parameters.try_emplace(key, MakeFloatParameter(defaultValue));
	if (inserted) {
		parametersInserted_ = true;
	}
	ParticleMaterialAnimatedParameter& parameter = it->second;
	parameter.componentCount = 1;

	bool changed = false;
	ImGui::PushID(key);
	if (MyGUI::CollapsingHeader(label, false)) {

		changed |= MyGUI::EnumCombo("更新方法", parameter.mode).valueChanged;
		const FloatEditSetting editSetting = ParticleGUI::MakeDragSetting(minValue, maxValue, dragSpeed);
		if (parameter.mode == ParticleMaterialParameterMode::Constant) {

			changed |= MyGUI::DragFloat("定数", parameter.constant.x, editSetting).valueChanged;
		} else {

			changed |= MyGUI::DragFloat("開始", parameter.start.x, editSetting).valueChanged;
			changed |= MyGUI::DragFloat("終了", parameter.end.x, editSetting).valueChanged;
			changed |= ParticleGUI::DrawInterpolationEasing(parameter.easingType);
			changed |= MyGUI::Checkbox("カーブを使用", parameter.useCurve);
			if (parameter.useCurve) {

				CurveEditSetting setting{};
				setting.size = ImVec2(0.0f, 260.0f);
				setting.fixedTimeRange = true;
				ParameterUIState& uiState = uiStates_[key];
				changed |= MyGUI::CurveEditor("Curve", parameter.curveW,
					uiState.curveState, setting).valueChanged;
				if (MyGUI::CollapsingHeader("カーブ生成", false)) {

					static const CurveBakeTarget targets[] = { { "値", { 0u } } };
					changed |= DrawCurveGenerator(uiState.curveGeneratorState,
						GetCurveChannels(parameter.curveW), targets);
				}
			}
			changed |= ParticleGUI::DrawLoopSettings(parameter.loop);
		}
	}
	ImGui::PopID();
	return changed;
}

bool Engine::ParticleShapeOverLifetimeModuleDrawer::DrawColorParameter(
	const char* label, const char* key, const Color4& defaultValue) {

	auto [it, inserted] = settings_.parameters.try_emplace(key, MakeColorParameter(defaultValue));
	if (inserted) {
		parametersInserted_ = true;
	}
	ParticleMaterialAnimatedParameter& parameter = it->second;
	parameter.componentCount = 4;

	bool changed = false;
	ImGui::PushID(key);
	if (MyGUI::CollapsingHeader(label, false)) {

		changed |= MyGUI::EnumCombo("更新方法", parameter.mode).valueChanged;
		auto drawColor = [&](const char* colorLabel, Vector4& value) {

			Color4 color = ToColor4(value);
			if (MyGUI::ColorEdit(colorLabel, color).valueChanged) {

				value = ToVector4(color);
				return true;
			}
			return false;
		};
		if (parameter.mode == ParticleMaterialParameterMode::Constant) {

			changed |= drawColor("定数", parameter.constant);
		} else {

			changed |= drawColor("開始", parameter.start);
			changed |= drawColor("終了", parameter.end);
			changed |= ParticleGUI::DrawInterpolationEasing(parameter.easingType);
			changed |= MyGUI::Checkbox("カーブを使用", parameter.useCurve);
			if (parameter.useCurve) {

				std::array<CurveChannelRef, 4> refs = {
					CurveChannelRef{ &parameter.curve3.channels[0], "R" },
					CurveChannelRef{ &parameter.curve3.channels[1], "G" },
					CurveChannelRef{ &parameter.curve3.channels[2], "B" },
					CurveChannelRef{ &parameter.curveW.channel, "A" },
				};
				CurveEditSetting setting{};
				setting.size = ImVec2(0.0f, 260.0f);
				setting.fixedTimeRange = true;
				ParameterUIState& uiState = uiStates_[key];
				changed |= MyGUI::CurveEditor("Curve", std::span(refs),
					uiState.curveState, setting).valueChanged;
				if (MyGUI::CollapsingHeader("カーブ生成", false)) {

					static const CurveBakeTarget colorTargets[] = {
						{ "RGB", { 0u, 1u, 2u } }, { "R", { 0u } },
						{ "G", { 1u } }, { "B", { 2u } } };
					changed |= DrawCurveGenerator(uiState.curveGeneratorState,
						parameter.curve3.channels, colorTargets);

					ImGui::SeparatorText("A");
					static const CurveBakeTarget alphaTargets[] = { { "値", { 0u } } };
					changed |= DrawCurveGenerator(uiState.alphaGeneratorState,
						GetCurveChannels(parameter.curveW), alphaTargets);
				}
			}
			changed |= ParticleGUI::DrawLoopSettings(parameter.loop);
		}
	}
	ImGui::PopID();
	return changed;
}
