#include "ParticleMotionSettings.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Engine;

namespace {

	// 非有限値を保存設定から除外する
	float ReadFinite(const nlohmann::json& in, const char* name, float fallback) {

		float value = in.value(name, fallback);
		if (!std::isfinite(value)) throw std::invalid_argument("粒子の運動設定が不正です");
		return value;
	}

	// 位置と方向の非有限値を拒否する
	Vector3 ReadVector(const nlohmann::json& in, const char* name, Vector3 fallback) {

		if (!in.contains(name)) return fallback;
		const auto value = Vector3::FromJson(in.at(name));
		if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
			throw std::invalid_argument("粒子の方向設定が不正です");
		return value;
	}
}

void Engine::from_json(const nlohmann::json& in, ParticleForceFieldSettings& settings) {

	const auto type = in.value("type", 0u);
	if (type > 3) throw std::invalid_argument("力場の種類が不正です");
	settings.type = static_cast<ParticleForceFieldType>(type);
	settings.center = ReadVector(in, "center", settings.center);
	settings.axis = Vector3::NormalizeOr(ReadVector(in, "axis", settings.axis), Vector3(0, 1, 0));
	settings.strength = ReadFinite(in, "strength", settings.strength);
	settings.range = (std::max)(0.001f, ReadFinite(in, "range", settings.range));
	settings.falloff = (std::max)(0.0f, ReadFinite(in, "falloff", settings.falloff));
	settings.layerMask = in.value("layerMask", settings.layerMask);
	settings.useSceneFields = in.value("useSceneFields", settings.useSceneFields);
}

void Engine::to_json(nlohmann::json& out, const ParticleForceFieldSettings& settings) {

	out = {{"type", settings.type}, {"center", settings.center.ToJson()}, {"axis", settings.axis.ToJson()},
		{"strength", settings.strength}, {"range", settings.range}, {"falloff", settings.falloff},
		{"layerMask", settings.layerMask}, {"useSceneFields", settings.useSceneFields}};
}

void Engine::from_json(const nlohmann::json& in, ParticleVectorFieldSettings& settings) {

	settings.intensity = ReadFinite(in, "intensity", settings.intensity);
	settings.tightness = std::clamp(ReadFinite(in, "tightness", settings.tightness), 0.0f, 1.0f);
	settings.layerMask = in.value("layerMask", settings.layerMask);
}

void Engine::to_json(nlohmann::json& out, const ParticleVectorFieldSettings& settings) {

	out = {{"intensity", settings.intensity}, {"tightness", settings.tightness}, {"layerMask", settings.layerMask}};
}

void Engine::from_json(const nlohmann::json& in, ParticleSplineMovementSettings& settings) {

	if (in.contains("spline")) in.at("spline").get_to(settings.spline);
	settings.speed = ReadFinite(in, "speed", settings.speed);
	settings.startDistance = ReadFinite(in, "startDistance", settings.startDistance);
	settings.orbitRadius = (std::max)(0.0f, ReadFinite(in, "orbitRadius", settings.orbitRadius));
	settings.orbitTurnsPerMeter = ReadFinite(in, "orbitTurnsPerMeter", settings.orbitTurnsPerMeter);
	settings.loop = in.value("loop", settings.loop);
	settings.orientToTangent = in.value("orientToTangent", settings.orientToTangent);
}

void Engine::to_json(nlohmann::json& out, const ParticleSplineMovementSettings& settings) {

	out = {{"spline", settings.spline}, {"speed", settings.speed}, {"startDistance", settings.startDistance},
		{"orbitRadius", settings.orbitRadius}, {"orbitTurnsPerMeter", settings.orbitTurnsPerMeter},
		{"loop", settings.loop}, {"orientToTangent", settings.orientToTangent}};
}

void Engine::from_json(const nlohmann::json& in, ParticleCollisionSettings& settings) {

	settings.enabled = in.value("enabled", settings.enabled);
	settings.sceneColliders = in.value("sceneColliders", settings.sceneColliders);
	settings.plane = in.value("plane", settings.plane);
	settings.depth = in.value("depth", settings.depth);
	settings.heightmap = in.value("heightmap", settings.heightmap);
	settings.includeTriggers = in.value("includeTriggers", settings.includeTriggers);
	settings.planeNormal = Vector3::NormalizeOr(ReadVector(in, "planeNormal", settings.planeNormal), Vector3(0, 1, 0));
	settings.planeDistance = ReadFinite(in, "planeDistance", settings.planeDistance);
	settings.radius = (std::max)(0.0f, ReadFinite(in, "radius", settings.radius));
	settings.bounce = std::clamp(ReadFinite(in, "bounce", settings.bounce), 0.0f, 1.0f);
	settings.friction = std::clamp(ReadFinite(in, "friction", settings.friction), 0.0f, 1.0f);
	settings.depthThickness = (std::max)(0.001f, ReadFinite(in, "depthThickness", settings.depthThickness));
	settings.layerMask = in.value("layerMask", settings.layerMask);
	const auto response = in.value("response", 0u);
	if (response > 2) throw std::invalid_argument("粒子の衝突応答が不正です");
	settings.response = static_cast<ParticleCollisionResponse>(response);
	if (in.contains("depthCamera")) in.at("depthCamera").get_to(settings.depthCamera);
}

void Engine::to_json(nlohmann::json& out, const ParticleCollisionSettings& settings) {

	out = {{"enabled", settings.enabled}, {"sceneColliders", settings.sceneColliders}, {"plane", settings.plane},
		{"depth", settings.depth}, {"heightmap", settings.heightmap}, {"includeTriggers", settings.includeTriggers},
		{"planeNormal", settings.planeNormal.ToJson()}, {"planeDistance", settings.planeDistance},
		{"radius", settings.radius}, {"bounce", settings.bounce}, {"friction", settings.friction},
		{"depthThickness", settings.depthThickness}, {"layerMask", settings.layerMask},
		{"response", settings.response}, {"depthCamera", settings.depthCamera}};
}
