#include "ParticleFieldComponents.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/AffineDecompose.h>

// c++
#include <algorithm>
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
	template<class T>
	void ReadBounds(const nlohmann::json& in, T& field, ParticleFieldShape fallback) {

		const auto shape = in.value("shape", static_cast<uint32_t>(fallback));
		if (shape > static_cast<uint32_t>(ParticleFieldShape::OBB)) throw std::invalid_argument("場の形状が不正です");
		field.shape = static_cast<ParticleFieldShape>(shape);
		field.size = ReadSize(in, field.size);
		field.drawDebug = in.value("drawDebug", true);
	}
}

bool ParticleFieldBounds::Build(ParticleFieldShape shape, float radius, Vector3 size, const Matrix4x4& world) {

	Vector3 scale;
	if (!DecomposeAffine3D(world, center, rotation, scale)) return false;
	scale = {Vector3::TransferNormal({1, 0, 0}, world).Length(), Vector3::TransferNormal({0, 1, 0}, world).Length(),
		Vector3::TransferNormal({0, 0, 1}, world).Length()};
	if (shape != ParticleFieldShape::OBB) rotation = Quaternion::Identity();
	halfSize = shape == ParticleFieldShape::Sphere ? Vector3::AnyInit(radius * (std::max)({scale.x, scale.y, scale.z})) :
		Vector3(size.x * scale.x, size.y * scale.y, size.z * scale.z) * 0.5f;
	return std::isfinite(halfSize.x) && std::isfinite(halfSize.y) && std::isfinite(halfSize.z) &&
		0 < halfSize.x && 0 < halfSize.y && 0 < halfSize.z;
}

Matrix4x4 ParticleFieldBounds::UnitToWorld() const {

	return Matrix4x4::MakeAffineMatrix(halfSize, rotation, center);
}

void Engine::from_json(const nlohmann::json& in, ForceFieldComponent& component) {

	ParticleForceFieldSettings settings;
	from_json(in, settings);
	component.type = settings.type;
	component.axis = settings.axis;
	component.strength = settings.strength;
	component.range = settings.range;
	ReadBounds(in, component, ParticleFieldShape::Sphere);
	component.falloff = settings.falloff;
	component.layerMask = in.value("layerMask", 1u);
	component.enabled = in.value("enabled", true);
}

void Engine::to_json(nlohmann::json& out, const ForceFieldComponent& component) {

	out = {{"type", component.type}, {"axis", component.axis.ToJson()}, {"strength", component.strength},
		{"shape", component.shape}, {"size", component.size.ToJson()}, {"drawDebug", component.drawDebug},
		{"range", component.range}, {"falloff", component.falloff}, {"layerMask", component.layerMask}, {"enabled", component.enabled}};
}

void Engine::from_json(const nlohmann::json& in, VectorFieldComponent& component) {

	const auto preset = in.value("preset", 0u);
	if (preset > 2) throw std::invalid_argument("ベクトル場の種類が不正です");
	ParticleVectorFieldSettings settings;
	from_json(in, settings);
	component.field = ParseAssetID(in, "field");
	ReadBounds(in, component, ParticleFieldShape::OBB);
	component.range = in.value("range", 5.0f);
	if (!std::isfinite(component.range) || component.range <= 0) throw std::invalid_argument("場の半径が不正です");
	component.preset = static_cast<ParticleVectorFieldPreset>(preset);
	component.intensity = settings.intensity;
	component.tightness = settings.tightness;
	component.layerMask = in.value("layerMask", 1u);
	component.enabled = in.value("enabled", true);
}

void Engine::to_json(nlohmann::json& out, const VectorFieldComponent& component) {

	out = {{"shape", component.shape}, {"range", component.range}, {"drawDebug", component.drawDebug},
		{"field", ToString(component.field)}, {"size", component.size.ToJson()}, {"preset", component.preset},
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
