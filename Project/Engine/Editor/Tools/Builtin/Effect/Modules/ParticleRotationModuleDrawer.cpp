#include "ParticleRotationModuleDrawer.h"

#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>
#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

#include <algorithm>
#include <span>

using namespace Engine;

namespace {

	// 回転モードを日本語で選択する
	bool DrawRotationMode(Engine::ParticleRotationMode& mode) {

		const char* labels[] = { "固定角度", "角度加算", "角度補間" };
		int32_t current = static_cast<int32_t>(mode);
		if (!Engine::MyGUI::BeginPropertyRow("回転モード")) {
			return false;
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		const bool changed = ImGui::Combo("##Value", &current, labels, IM_ARRAYSIZE(labels));
		Engine::MyGUI::EndPropertyRow();
		if (changed) {
			mode = static_cast<Engine::ParticleRotationMode>(current);
		}
		return changed;
	}

	// 回転速度モードを日本語で選択する
	bool DrawRotationSpeedMode(Engine::ParticleRotationSpeedMode& mode) {

		const char* labels[] = { "定数", "オーバーライフタイム" };
		int32_t current = static_cast<int32_t>(mode);
		if (!Engine::MyGUI::BeginPropertyRow("回転速度")) {
			return false;
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		const bool changed = ImGui::Combo("##Value", &current, labels, IM_ARRAYSIZE(labels));
		Engine::MyGUI::EndPropertyRow();
		if (changed) {
			mode = static_cast<Engine::ParticleRotationSpeedMode>(current);
		}
		return changed;
	}

	// 角度の入力形式を日本語で選択する
	bool DrawRotationValueType(Engine::ParticleRotationValueType& type) {

		const char* labels[] = { "オイラー角", "Quaternion" };
		int32_t current = static_cast<int32_t>(type);
		if (!Engine::MyGUI::BeginPropertyRow("角度形式")) {
			return false;
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		const bool changed = ImGui::Combo("##Value", &current, labels, IM_ARRAYSIZE(labels));
		Engine::MyGUI::EndPropertyRow();
		if (changed) {
			type = static_cast<Engine::ParticleRotationValueType>(current);
		}
		return changed;
	}

	// ランダム範囲を持つAxisとAngleを編集する
	bool DrawAxisAngleValue(const char* label, Engine::Vector3& axis,
		Engine::ParticleValue<float>& angle) {

		ImGui::PushID(label);
		ImGui::SeparatorText(label);
		bool changed = Engine::MyGUI::DragVector3("Axis", axis,
			Engine::ParticleGUI::MakeDragSetting(-1.0f, 1.0f)).valueChanged;
		changed |= Engine::ParticleGUI::DrawParticleValueFloat("Angle", angle,
			Engine::ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f, 0.5f));
		ImGui::PopID();
		return changed;
	}

	// AxisとAngleを編集する
	bool DrawAxisAngle(const char* label, Engine::Vector3& axis, float& angle) {

		ImGui::PushID(label);
		ImGui::SeparatorText(label);
		bool changed = Engine::MyGUI::DragVector3("Axis", axis,
			Engine::ParticleGUI::MakeDragSetting(-1.0f, 1.0f)).valueChanged;
		changed |= Engine::MyGUI::DragFloat("Angle", angle,
			Engine::ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f, 0.5f)).valueChanged;
		ImGui::PopID();
		return changed;
	}
}

bool ParticleRotationModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleRotationModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto settings = concrete->GetSettings();

	bool changed = DrawRotationMode(settings.mode);
	changed |= DrawRotationValueType(settings.valueType);
	if (settings.mode == ParticleRotationMode::Fixed) {

		if (settings.valueType == ParticleRotationValueType::Euler) {
			changed |= ParticleGUI::DrawParticleValueVector3("固定角度", settings.fixedAngle,
				ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f, 0.5f));
		} else {
			changed |= DrawAxisAngleValue("固定角度", settings.fixedAxis, settings.fixedQuaternionAngle);
		}
	} else if (settings.mode == ParticleRotationMode::Additive) {
		changed |= DrawAdditiveSettings(settings);
	} else {
		changed |= DrawInterpolationSettings(settings);
	}
	if (changed) {
		concrete->SetSettings(settings);
	}
	return changed;

}

bool ParticleRotationModuleDrawer::DrawAdditiveSettings(ParticleRotationModule::Settings& settings) {

	bool changed = false;
	if (settings.valueType == ParticleRotationValueType::Euler) {
		changed |= ParticleGUI::DrawParticleValueVector3("加算角度", settings.addAngle,
			ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f, 0.5f));
	} else {
		changed |= DrawAxisAngleValue("加算角度", settings.addAxis, settings.addQuaternionAngle);
	}
	changed |= DrawRotationSpeedMode(settings.speedMode);
	if (settings.speedMode == ParticleRotationSpeedMode::Constant) {

		if (settings.valueType == ParticleRotationValueType::Euler) {
			changed |= ParticleGUI::DrawParticleValueVector3("回転速度", settings.rotationSpeed,
				ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f, 0.5f));
		} else {
			changed |= DrawAxisAngleValue(
				"回転速度", settings.rotationSpeedAxis, settings.rotationQuaternionSpeed);
		}
		return changed;
	}

	if (settings.valueType == ParticleRotationValueType::Euler) {

		changed |= MyGUI::DragVector3("開始回転速度", settings.startSpeed,
			ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f, 0.5f)).valueChanged;
		changed |= MyGUI::DragVector3("終了回転速度", settings.endSpeed,
			ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f, 0.5f)).valueChanged;
	} else {

		changed |= DrawAxisAngle("開始回転速度", settings.startSpeedAxis, settings.startQuaternionSpeed);
		changed |= DrawAxisAngle("終了回転速度", settings.endSpeedAxis, settings.endQuaternionSpeed);
	}
	changed |= ParticleGUI::DrawInterpolationEasing(settings.speedEasingType);
	changed |= MyGUI::Checkbox("カーブを使用", settings.useSpeedCurve);
	if (settings.useSpeedCurve) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		if (settings.valueType == ParticleRotationValueType::Euler) {
			changed |= MyGUI::CurveEditor("RotationSpeedCurve", settings.speedCurve, speedCurveState_, setting).valueChanged;
		} else {
			changed |= MyGUI::CurveEditor("RotationSpeedQuaternionCurve",
				settings.speedQuaternionCurve, speedQuaternionCurveState_, setting).valueChanged;
		}

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			if (settings.valueType == ParticleRotationValueType::Euler) {

				static const CurveBakeTarget targets[] = {
					{ "XYZ", { 0u, 1u, 2u } }, { "X", { 0u } }, { "Y", { 1u } }, { "Z", { 2u } } };
				changed |= DrawCurveGenerator(speedGeneratorState_, settings.speedCurve.channels, targets);
			} else {

				static const CurveBakeTarget targets[] = { { "Angle", { 1u } } };
				changed |= DrawCurveGenerator(speedQuaternionGeneratorState_,
					settings.speedQuaternionCurve.channels, targets);
			}
		}
	}
	changed |= ParticleGUI::DrawLoopSettings(settings.speedLoop);
	return changed;
}

bool ParticleRotationModuleDrawer::DrawInterpolationSettings(ParticleRotationModule::Settings& settings) {

	bool changed = false;
	if (settings.valueType == ParticleRotationValueType::Euler) {

		changed |= MyGUI::DragVector3("開始角度", settings.startAngle,
			ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f, 0.5f)).valueChanged;
		changed |= MyGUI::DragVector3("終了角度", settings.endAngle,
			ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f, 0.5f)).valueChanged;
	} else {

		changed |= DrawAxisAngle("開始角度", settings.startAxis, settings.startQuaternionAngle);
		changed |= DrawAxisAngle("終了角度", settings.endAxis, settings.endQuaternionAngle);
	}
	changed |= ParticleGUI::DrawInterpolationEasing(settings.angleEasingType);
	changed |= MyGUI::Checkbox("カーブを使用", settings.useAngleCurve);
	if (settings.useAngleCurve) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		if (settings.valueType == ParticleRotationValueType::Euler) {
			changed |= MyGUI::CurveEditor("RotationAngleCurve", settings.angleCurve, angleCurveState_, setting).valueChanged;
		} else {
			changed |= MyGUI::CurveEditor(
				"RotationQuaternionCurve", settings.quaternionCurve, quaternionCurveState_, setting).valueChanged;
		}

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			if (settings.valueType == ParticleRotationValueType::Euler) {

				static const CurveBakeTarget targets[] = {
					{ "XYZ", { 0u, 1u, 2u } }, { "X", { 0u } }, { "Y", { 1u } }, { "Z", { 2u } } };
				changed |= DrawCurveGenerator(angleGeneratorState_, settings.angleCurve.channels, targets);
			} else {

				static const CurveBakeTarget targets[] = { { "Angle", { 1u } } };
				changed |= DrawCurveGenerator(
					quaternionGeneratorState_, settings.quaternionCurve.channels, targets);
			}
		}
	}
	changed |= ParticleGUI::DrawLoopSettings(settings.angleLoop);
	return changed;
}
