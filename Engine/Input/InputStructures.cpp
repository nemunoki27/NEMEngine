#include "InputStructures.h"

//============================================================================
//	include
//============================================================================

// エディター
#include <imgui.h>

//============================================================================
//	InputStructures classMethods
//============================================================================

void InputVibrationParams::ImGui(const char* label) {

	ImGui::SeparatorText(label);
	ImGui::PushID(label);

	ImGui::DragFloat("Left Motor", &left, 0.01f);
	ImGui::DragFloat("Right Motor", &right, 0.01f);
	ImGui::DragFloat("Duration", &duration, 0.01f);
	ImGui::DragFloat("Attack", &attack, 0.01f);
	ImGui::DragFloat("Release", &release, 0.01f);
	ImGui::DragInt("Priority", &priority, 1);

	ImGui::PopID();
}

void InputVibrationParams::FromJson(const nlohmann::json& data) {

	if (data.empty()) {
		return;
	}
	left = data.value("left", 0.0f);
	right = data.value("right", 0.0f);
	duration = data.value("duration", 0.0f);
	attack = data.value("attack", 0.0f);
	release = data.value("release", 0.0f);
	priority = data.value("priority", 0);
}

void InputVibrationParams::ToJson(nlohmann::json& data) {

	data["left"] = left;
	data["right"] = right;
	data["duration"] = duration;
	data["attack"] = attack;
	data["release"] = release;
	data["priority"] = priority;
}