#include "ParticleCustomDataSettings.h"

void Engine::to_json(nlohmann::json& out, const ParticleCustomDataSettings& value) {

	out = nlohmann::json::object();
	to_json(out["customData1"], value.customData1);
	to_json(out["customData2"], value.customData2);
}

void Engine::from_json(const nlohmann::json& in, ParticleCustomDataSettings& value) {

	if (!in.is_object()) return;
	value.enabled = true;
	if (const auto it = in.find("customData1"); it != in.end()) from_json(*it, value.customData1);
	if (const auto it = in.find("customData2"); it != in.end()) from_json(*it, value.customData2);
	value.customData1.componentCount = value.customData2.componentCount = 4;
}

float Engine::ParticleStableRandom(uint64_t birthID) {

	const auto hash = [](uint32_t value) {
		value ^= value >> 16;
		value *= 0x7feb352du;
		value ^= value >> 15;
		value *= 0x846ca68bu;
		return value ^ (value >> 16);
	};
	return float(hash(uint32_t(birthID) ^ hash(uint32_t(birthID >> 32))) >> 8) * (1.0f / 16777216.0f);
}
