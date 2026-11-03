#include "MotionAnimationComponent.h"

//============================================================================
//	include
//============================================================================

// c++
#include <cmath>
#include <stdexcept>

using namespace Engine;

void Engine::to_json(nlohmann::json& out, const MotionAnimationSettings& candidate) {

	out = {{"enabled", candidate.enabled}, {"playOnStart", candidate.playOnStart}, {"playInEditMode", candidate.playInEditMode},
		{"autoApply", candidate.autoApply}, {"applyPosition", candidate.applyPosition}, {"applyRotation", candidate.applyRotation},
		{"trackTarget", candidate.trackTarget}, {"drawDebug", candidate.drawDebug}, {"target", candidate.target}, {"spline", candidate.spline},
		{"timeSource", candidate.timeSource}, {"mode", candidate.mode}, {"plane", candidate.plane}, {"rotation", candidate.rotation},
		{"rotationEdit", candidate.rotationEdit}, {"repeat", candidate.repeat}, {"offset", candidate.offset.ToJson()},
		{"euler", candidate.euler.ToJson()}, {"axis", candidate.axis.ToJson()}, {"angle", candidate.angle},
		{"stoppingDistance", candidate.stoppingDistance}, {"splineStart", candidate.splineStart}, {"splineEnd", candidate.splineEnd},
		{"duration", candidate.duration}, {"delay", candidate.delay}, {"speed", candidate.speed}, {"cycleCount", candidate.cycleCount},
		{"easing", candidate.easing}, {"segments", nlohmann::json::array()}};
	for (const auto& segment : candidate.segments) out["segments"].push_back({{"offset", segment.offset.ToJson()},
		{"duration", segment.duration}, {"easing", segment.easing}});
}

void Engine::from_json(const nlohmann::json& in, MotionAnimationSettings& settings) {

	MotionAnimationSettings candidate;
	candidate.enabled = in.value("enabled", true);
	candidate.playOnStart = in.value("playOnStart", true);
	candidate.playInEditMode = in.value("playInEditMode", false);
	candidate.autoApply = in.value("autoApply", true);
	candidate.applyPosition = in.value("applyPosition", true);
	candidate.applyRotation = in.value("applyRotation", false);
	candidate.trackTarget = in.value("trackTarget", false);
	candidate.drawDebug = in.value("drawDebug", true);
	candidate.target = in.value("target", SceneEntityReference{}); candidate.spline = in.value("spline", SceneEntityReference{});
	candidate.timeSource = in.value("timeSource", TimeSourceSettings{});
	candidate.mode = in.value("mode", MotionAnimationMode::Relative);
	candidate.plane = in.value("plane", MotionAnimationPlane::Full3D);
	candidate.rotation = in.value("rotation", MotionAnimationRotation::Relative);
	candidate.rotationEdit = in.value("rotationEdit", SimpleAnimationRotationMode::Euler);
	candidate.repeat = in.value("repeat", SimpleAnimationRepeat::Once);
	if (in.contains("offset")) candidate.offset = Vector3::FromJson(in.at("offset"));
	if (in.contains("euler")) candidate.euler = Vector3::FromJson(in.at("euler"));
	if (in.contains("axis")) candidate.axis = Vector3::FromJson(in.at("axis"));
	candidate.angle = in.value("angle", 0.0f);
	candidate.stoppingDistance = in.value("stoppingDistance", 1.0f);
	candidate.splineStart = in.value("splineStart", 0.0f);
	candidate.splineEnd = in.value("splineEnd", 1.0f);
	candidate.duration = in.value("duration", 1.0f);
	candidate.delay = in.value("delay", 0.0f);
	candidate.speed = in.value("speed", 1.0f);
	candidate.cycleCount = in.value("cycleCount", 0u);
	candidate.easing = in.value("easing", EasingType::Linear);
	const auto finiteVector = [](Vector3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
	const auto validEasing = [](EasingType v) { return static_cast<unsigned>(v) <= static_cast<unsigned>(EasingType::EaseInOutBounce); };
	if (static_cast<unsigned>(candidate.mode) > 3 || static_cast<unsigned>(candidate.plane) > 2 ||
		static_cast<unsigned>(candidate.rotation) > 2 ||
		static_cast<unsigned>(candidate.repeat) > 2 || static_cast<unsigned>(candidate.rotationEdit) > 1 || !validEasing(candidate.easing) ||
		!finiteVector(candidate.offset) || !finiteVector(candidate.euler) || !finiteVector(candidate.axis) || !std::isfinite(candidate.angle) ||
		!std::isfinite(candidate.duration) || candidate.duration < 0.0001f || !std::isfinite(candidate.delay) || candidate.delay < 0 ||
		!std::isfinite(candidate.speed) || candidate.speed < 0 || !std::isfinite(candidate.stoppingDistance) || candidate.stoppingDistance < 0 ||
		!std::isfinite(candidate.splineStart) || !std::isfinite(candidate.splineEnd)) throw std::invalid_argument("MotionAnimationの設定が不正です");
	if (in.contains("segments")) {
		const auto& values = in.at("segments");
		if (!values.is_array() || values.empty() || values.size() > 1024) throw std::invalid_argument("MotionAnimationの区間数が不正です");
		candidate.segments.clear();
		for (const auto& value : values) {
			MotionAnimationSegment segment;
			segment.offset = Vector3::FromJson(value.at("offset"));
			segment.duration = value.value("duration", 1.0f);
			segment.easing = value.value("easing", EasingType::Linear);
			if (!finiteVector(segment.offset) || !std::isfinite(segment.duration) || segment.duration < 0.0001f || !validEasing(segment.easing))
				throw std::invalid_argument("MotionAnimationの区間設定が不正です");
			candidate.segments.push_back(segment);
		}
	}
	settings = std::move(candidate);
}
