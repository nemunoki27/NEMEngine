#include "ParticleGuiHelpers.h"

//============================================================================
//	include
//============================================================================

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

	changed |= MyGUI::EnumCombo<ParticleValueType>("タイプ", value.type).valueChanged;

	if (value.type == ParticleValueType::Constant) {

		changed |= MyGUI::DragFloat(label, value.constant, setting).valueChanged;
	} else {

		changed |= MyGUI::DragFloat((std::string(label) + " 最小").c_str(), value.min, setting).valueChanged;
		changed |= MyGUI::DragFloat((std::string(label) + " 最大").c_str(), value.max, setting).valueChanged;
	}
	ImGui::PopID();
	return changed;
}

bool Engine::ParticleGui::DrawParticleValueUInt(const char* label, ParticleValue<uint32_t>& value) {

	bool changed = false;
	ImGui::PushID(label);

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
		changed |= dragUInt(label, value.constant);
	} else {

		changed |= dragUInt((std::string(label) + " 最小").c_str(), value.min);
		changed |= dragUInt((std::string(label) + " 最大").c_str(), value.max);
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

bool Engine::ParticleGui::SelectEasing(EasingType& easing) {

	const EasingType previous = easing;
	Easing::SelectEasingType(easing, "easing");
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
