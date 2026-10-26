#include "ParticleFieldComponents.h"

//============================================================================
//	include
//============================================================================
// c++
#include <cmath>
#include <stdexcept>

using namespace Engine;

namespace {

	// ゼロサイズと非有限値の境界を拒否する
	Vector3 ReadSize(const nlohmann::json& in, Vector3 fallback) {

		const auto size = in.contains("size") ? Vector3::FromJson(in.at("size")) : fallback;
		if (!std::isfinite(size.x) || !std::isfinite(size.y) || !std::isfinite(size.z) ||
			size.x <= 0 || size.y <= 0 || size.z <= 0) throw std::invalid_argument("場のサイズが不正です");
		return size;
	}
}

void Engine::from_json(const nlohmann::json& in, ForceFieldComponent& component) {

	ParticleForceFieldSettings settings;
	from_json(in, settings);
	component.type = settings.type;
	component.axis = settings.axis;
	component.strength = settings.strength;
	component.range = settings.range;
	component.falloff = settings.falloff;
	component.layerMask = in.value("layerMask", 1u);
	component.enabled = in.value("enabled", true);
}

void Engine::to_json(nlohmann::json& out, const ForceFieldComponent& component) {

	out = {{"type", component.type}, {"axis", component.axis.ToJson()}, {"strength", component.strength},
		{"range", component.range}, {"falloff", component.falloff}, {"layerMask", component.layerMask}, {"enabled", component.enabled}};
}

void Engine::from_json(const nlohmann::json& in, VectorFieldComponent& component) {

	const auto preset = in.value("preset", 0u);
	if (preset > 2) throw std::invalid_argument("ベクトル場の種類が不正です");
	ParticleVectorFieldSettings settings;
	from_json(in, settings);
	component.field = ParseAssetID(in, "field");
	component.size = ReadSize(in, component.size);
	component.preset = static_cast<ParticleVectorFieldPreset>(preset);
	component.intensity = settings.intensity;
	component.tightness = settings.tightness;
	component.layerMask = in.value("layerMask", 1u);
	component.enabled = in.value("enabled", true);
}

void Engine::to_json(nlohmann::json& out, const VectorFieldComponent& component) {

	out = {{"field", ToString(component.field)}, {"size", component.size.ToJson()}, {"preset", component.preset},
		{"intensity", component.intensity}, {"tightness", component.tightness},
		{"layerMask", component.layerMask}, {"enabled", component.enabled}};
}

void Engine::from_json(const nlohmann::json& in, HeightmapColliderComponent& component) {

	component.heightmap = ParseAssetID(in, "heightmap");
	component.size = ReadSize(in, component.size);
	component.layerMask = in.value("layerMask", 1u);
	component.enabled = in.value("enabled", true);
}

void Engine::to_json(nlohmann::json& out, const HeightmapColliderComponent& component) {

	out = {{"heightmap", ToString(component.heightmap)}, {"size", component.size.ToJson()},
		{"layerMask", component.layerMask}, {"enabled", component.enabled}};
}
