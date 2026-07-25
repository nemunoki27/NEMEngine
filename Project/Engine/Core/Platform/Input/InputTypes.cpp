#include "InputTypes.h"

//============================================================================
//	InputStructures classMethods
//============================================================================
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
