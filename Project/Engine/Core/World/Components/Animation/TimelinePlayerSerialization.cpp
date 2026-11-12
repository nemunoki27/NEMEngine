#include "TimelinePlayerComponent.h"

//============================================================================
//	include
//============================================================================

// c++
#include <cmath>
#include <stdexcept>

using namespace Engine;

void Engine::to_json(nlohmann::json& out, const TimelinePlayerSettings& settings) {

	out = {{"enabled", settings.enabled}, {"playOnStart", settings.playOnStart},
		{"playInEditMode", settings.playInEditMode}, {"timeline", ToAssetReferenceJson(settings.timeline)},
		{"timeSource", settings.timeSource}, {"speed", settings.speed}, {"startDelay", settings.startDelay},
		{"loop", settings.loop}, {"priority", settings.priority},
		{"bindings", nlohmann::json::array()}, {"parameters", nlohmann::json::array()}};
	for (const auto& binding : settings.bindings) out["bindings"].push_back({
		{"slot", ToString(binding.slot)}, {"target", binding.target}});
	for (const auto& parameter : settings.parameters) {
		nlohmann::json value;
		Engine::to_json(value, parameter.value);
		out["parameters"].push_back({{"parameter", ToString(parameter.parameter)}, {"value", value}});
	}
}

void Engine::from_json(const nlohmann::json& in, TimelinePlayerSettings& settings) {

	TimelinePlayerSettings candidate;
	candidate.enabled = in.value("enabled", true);
	candidate.playOnStart = in.value("playOnStart", false);
	candidate.playInEditMode = in.value("playInEditMode", false);
	candidate.timeline = ParseAssetID(in, "timeline");
	candidate.timeSource = in.value("timeSource", TimeSourceSettings{});
	candidate.speed = in.value("speed", 1.0f);
	candidate.startDelay = in.value("startDelay", 0.0);
	candidate.loop = in.value("loop", false);
	candidate.priority = in.value("priority", 0);
	if (!std::isfinite(candidate.speed) || candidate.speed < 0.0f ||
		!std::isfinite(candidate.startDelay) || candidate.startDelay < 0.0)
		throw std::invalid_argument("TimelinePlayerの再生速度または開始待ち時間が不正です");
	for (const auto& value : in.value("bindings", nlohmann::json::array())) {
		const auto slot = TryParseUUID16Hex(value.value("slot", std::string{}));
		if (!slot) throw std::invalid_argument("TimelinePlayerの割り当て識別子が不正です");
		candidate.bindings.push_back({*slot, value.value("target", SceneEntityReference{}), {}});
	}
	for (const auto& value : in.value("parameters", nlohmann::json::array())) {
		const auto id = TryParseUUID16Hex(value.value("parameter", std::string{}));
		if (!id) throw std::invalid_argument("TimelinePlayerのパラメーター識別子が不正です");
		TimelineParameterOverride parameter{*id};
		Engine::from_json(value.at("value"), parameter.value);
		candidate.parameters.push_back(std::move(parameter));
	}
	settings = std::move(candidate);
}
