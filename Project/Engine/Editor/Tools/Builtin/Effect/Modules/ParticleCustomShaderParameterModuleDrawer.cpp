#include "ParticleCustomShaderParameterModuleDrawer.h"

#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace {

	// リフレクション情報と変数名から色パラメータか判定
	bool IsColorParameter(const Engine::ShaderConstantBufferVariable& variable) {

		if (variable.isColor) {
			return true;
		}
		constexpr std::string_view kColor = "color";
		return std::search(variable.name.begin(), variable.name.end(),
			kColor.begin(), kColor.end(),
			[](char lhs, char rhs) {
				const auto toLower = [](char value) {
					return value >= 'A' && value <= 'Z' ?
						static_cast<char>(value + ('a' - 'A')) : value;
					};
				return toLower(lhs) == rhs;
			}) != variable.name.end();
	}

	// パラメータの構成要素からカーブ編集用の参照を生成する
	std::array<Engine::CurveChannelRef, 4> BuildCurveChannelRefs(
		Engine::ParticleMaterialAnimatedParameter& parameter, bool isColor) {

		std::array<Engine::CurveChannelRef, 4> refs{};
		if (parameter.componentCount <= 1) {
			refs[0] = { &parameter.curveW.channel, "Value" };
			return refs;
		}

		static const char* vectorNames[] = { "X", "Y", "Z", "W" };
		static const char* colorNames[] = { "R", "G", "B", "A" };
		const char** names = isColor ? colorNames : vectorNames;
		const uint32_t xyzCount = (std::min)(parameter.componentCount, 3u);
		for (uint32_t i = 0; i < xyzCount; ++i) {
			refs[i] = { &parameter.curve3.channels[i], names[i] };
		}
		if (parameter.componentCount >= 4) {
			refs[3] = { &parameter.curveW.channel, names[3] };
		}
		return refs;
	}
}

bool Engine::ParticleCustomShaderParameterModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleCustomShaderParameterModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto parameters = concrete->GetParameters();

	if (reflectedParameters_.empty()) {
		ImGui::TextDisabled("アニメーション可能なカスタムパラメータがありません");
		return false;
	}

	bool changed = false;
	for (const ShaderConstantBufferVariable& variable : reflectedParameters_) {

		ImGui::PushID(variable.name.c_str());
		if (MyGUI::CollapsingHeader(variable.name.c_str(), false)) {

			ParticleMaterialAnimatedParameter& parameter = parameters[variable.name];
			parameter.componentCount = std::clamp(GetVariableComponentCount(variable), 1u, 4u);
			changed |= DrawParameter(variable, parameter, uiStates_[variable.name]);
		}
		ImGui::PopID();
	}
	if (changed) {
		concrete->SetParameters(parameters);
	}
	return changed;

}

void Engine::ParticleCustomShaderParameterModuleDrawer::SetReflectedParameters(
	ParticleCustomShaderParameterModule& module, const std::vector<ShaderConstantBufferVariable>& parameters) {

	reflectedParameters_ = parameters;
	auto values = module.GetParameters();
	for (const ShaderConstantBufferVariable& variable : reflectedParameters_) {
		values[variable.name].componentCount = std::clamp(GetVariableComponentCount(variable), 1u, 4u);
	}
	module.SetParameters(values);
}

bool Engine::ParticleCustomShaderParameterModuleDrawer::DrawParameter(
	const ShaderConstantBufferVariable& variable,
	ParticleMaterialAnimatedParameter& parameter, ParameterUIState& uiState) {

	bool changed = MyGUI::EnumCombo("更新方法", parameter.mode).valueChanged;
	auto drawValue = [&](const char* label, Vector4& value) {

		const uint32_t count = parameter.componentCount;
		if (IsColorParameter(variable) && count == 3) {
			Color3 color(value.x, value.y, value.z);
			if (MyGUI::ColorEdit(label, color).valueChanged) {
				value.x = color.r;
				value.y = color.g;
				value.z = color.b;
				return true;
			}
			return false;
		}
		if (IsColorParameter(variable) && count == 4) {
			Color4 color(value.x, value.y, value.z, value.w);
			if (MyGUI::ColorEdit(label, color).valueChanged) {
				value = Vector4(color.r, color.g, color.b, color.a);
				return true;
			}
			return false;
		}
		if (count == 1) {
			return MyGUI::DragFloat(label, value.x, ParticleGUI::MakeDragSetting(-10000.0f, 10000.0f)).valueChanged;
		}
		if (count == 2) {
			Vector2 vector(value.x, value.y);
			if (MyGUI::DragVector2(label, vector, ParticleGUI::MakeDragSetting(-10000.0f, 10000.0f)).valueChanged) {
				value.x = vector.x;
				value.y = vector.y;
				return true;
			}
			return false;
		}
		if (count == 3) {
			Vector3 vector(value.x, value.y, value.z);
			if (MyGUI::DragVector3(label, vector, ParticleGUI::MakeDragSetting(-10000.0f, 10000.0f)).valueChanged) {
				value.x = vector.x;
				value.y = vector.y;
				value.z = vector.z;
				return true;
			}
			return false;
		}
		return MyGUI::DragVector4(label, value, ParticleGUI::MakeDragSetting(-10000.0f, 10000.0f)).valueChanged;
		};

	if (parameter.mode == ParticleMaterialParameterMode::Constant) {
		changed |= drawValue("定数", parameter.constant);
		return changed;
	}

	changed |= drawValue("開始", parameter.start);
	changed |= drawValue("終了", parameter.end);
	changed |= ParticleGUI::DrawInterpolationEasing(parameter.easingType);
	changed |= MyGUI::Checkbox("カーブを使用", parameter.useCurve);
	if (parameter.useCurve) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		const bool isColor = IsColorParameter(variable) && parameter.componentCount >= 3;
		auto channelRefs = BuildCurveChannelRefs(parameter, isColor);
		changed |= MyGUI::CurveEditor("Curve", std::span(channelRefs.data(), parameter.componentCount),
			uiState.curveState, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			if (parameter.componentCount <= 1) {

				static const CurveBakeTarget targets[] = { { "値", { 0u } } };
				changed |= DrawCurveGenerator(uiState.curveWGeneratorState_,
					GetCurveChannels(parameter.curveW), targets);
			} else {

				const uint32_t xyzCount = (std::min)(parameter.componentCount, 3u);
				auto channels = std::span(parameter.curve3.channels.data(), xyzCount);
				if (isColor) {

					static const CurveBakeTarget targets[] = {
						{ "RGB", { 0u, 1u, 2u } }, { "R", { 0u } }, { "G", { 1u } }, { "B", { 2u } } };
					changed |= DrawCurveGenerator(uiState.curve3GeneratorState_, channels, targets);
				} else if (parameter.componentCount == 2) {

					static const CurveBakeTarget targets[] = {
						{ "XY", { 0u, 1u } }, { "X", { 0u } }, { "Y", { 1u } } };
					changed |= DrawCurveGenerator(uiState.curve3GeneratorState_, channels, targets);
				} else {

					static const CurveBakeTarget targets[] = {
						{ "XYZ", { 0u, 1u, 2u } }, { "X", { 0u } }, { "Y", { 1u } }, { "Z", { 2u } } };
					changed |= DrawCurveGenerator(uiState.curve3GeneratorState_, channels, targets);
				}

				if (parameter.componentCount >= 4) {

					ImGui::PushID("WCurveGenerator");
					ImGui::SeparatorText(isColor ? "A" : "W");
					static const CurveBakeTarget targets[] = { { "値", { 0u } } };
					changed |= DrawCurveGenerator(uiState.curveWGeneratorState_,
						GetCurveChannels(parameter.curveW), targets);
					ImGui::PopID();
				}
			}
		}
	}
	changed |= ParticleGUI::DrawLoopSettings(parameter.loop);
	return changed;
}
