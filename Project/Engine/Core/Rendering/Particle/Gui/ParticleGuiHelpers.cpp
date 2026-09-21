#include "ParticleGuiHelpers.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ImGui/ImGuiEnum.h>

// c++
#include <algorithm>
#include <string>

//============================================================================
//	ParticleGui functions
//============================================================================
Engine::FloatEditSetting Engine::ParticleGui::MakeDragSetting(float minValue, float maxValue, float dragSpeed) {

	FloatEditSetting setting{};
	setting.dragSpeed = dragSpeed;
	setting.minValue = minValue;
	setting.maxValue = maxValue;
	return setting;
}

bool Engine::ParticleGui::DrawParticleValueFloat(const char* label, ParticleValue<float>& value,
	const FloatEditSetting& setting) {

	bool changed = false;
	ImGui::PushID(label);

	ImGui::SeparatorText(label);

	changed |= MyGUI::EnumCombo<ParticleValueType>("タイプ", value.type).valueChanged;

	if (value.type == ParticleValueType::Constant) {

		changed |= MyGUI::DragFloat("定数値", value.constant, setting).valueChanged;
	} else {

		changed |= MyGUI::DragFloat("最小値", value.min, setting).valueChanged;
		changed |= MyGUI::DragFloat("最大値", value.max, setting).valueChanged;
	}
	ImGui::PopID();
	return changed;
}

bool Engine::ParticleGui::DrawParticleValueUInt(const char* label, ParticleValue<uint32_t>& value) {

	bool changed = false;
	ImGui::PushID(label);

	ImGui::SeparatorText(label);

	changed |= MyGUI::EnumCombo<ParticleValueType>("タイプ", value.type).valueChanged;

	auto dragUInt = [&](const char* dragLabel, uint32_t& target) {
		int32_t intValue = static_cast<int32_t>(target);
		if (MyGUI::DragInt(dragLabel, intValue).valueChanged) {

			target = static_cast<uint32_t>((std::max)(0, intValue));
			return true;
		}
		return false;
		};
	if (value.type == ParticleValueType::Constant) {
		changed |= dragUInt("定数値", value.constant);
	} else {

		changed |= dragUInt("最小値", value.min);
		changed |= dragUInt("最大値", value.max);
	}
	ImGui::PopID();
	return changed;
}

bool Engine::ParticleGui::DrawParticleValueVector3(const char* label, ParticleValue<Vector3>& value,
	const FloatEditSetting& setting) {

	bool changed = false;
	ImGui::PushID(label);

	changed |= MyGUI::EnumCombo<ParticleValueType>("タイプ", value.type).valueChanged;

	if (value.type == ParticleValueType::Constant) {

		changed |= MyGUI::DragVector3(label, value.constant, setting).valueChanged;
	} else {

		changed |= MyGUI::DragVector3((std::string(label) + " 最小").c_str(), value.min, setting).valueChanged;
		changed |= MyGUI::DragVector3((std::string(label) + " 最大").c_str(), value.max, setting).valueChanged;
	}
	ImGui::PopID();
	return changed;
}

bool Engine::ParticleGui::DrawLoopSettings(ParticleLoopSettings& loop) {

	bool changed = false;
	ImGui::SeparatorText("ループ");
	int32_t loopCount = loop.loopCount;
	if (MyGUI::DragInt("ループ回数", loopCount).valueChanged) {

		loop.loopCount = std::clamp(loopCount, 1, 64);
		changed = true;
	}
	changed |= MyGUI::EnumCombo<ParticleLoopType>("ループの種類", loop.type).valueChanged;
	return changed;
}

bool Engine::ParticleGui::DrawInterpolationEasing(EasingType& easing) {

	if (!MyGUI::BeginPropertyRow("イージング")) {
		return false;
	}

	const EasingType previous = easing;
	const float width = ImGui::GetContentRegionAvail().x;
	Easing::SelectEasingType(easing, "Value", width <= 1.0f ? 1.0f : width);
	MyGUI::EndPropertyRow();
	ImGui::Separator();
	return easing != previous;
}

bool Engine::ParticleGui::DragJsonFloat(const char* label, nlohmann::json& params, const char* key,
	float defaultValue, const FloatEditSetting& setting) {

	float value = params.value(key, defaultValue);
	if (MyGUI::DragFloat(label, value, setting).valueChanged) {

		params[key] = value;
		return true;
	}
	return false;
}
