#include "ParticleCone2DEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <algorithm>
#include <cmath>
#include <numbers>

//============================================================================
//	ParticleCone2DEmitterShape classMethods
//============================================================================
void Engine::ParticleCone2DEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	settings.cone.angle = data.value("coneAngle", settings.cone.angle);
	settings.cone.radius = data.value("coneRadius", settings.cone.radius);
}

void Engine::ParticleCone2DEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["coneAngle"] = settings.cone.angle;
	data["coneRadius"] = settings.cone.radius;
}

void Engine::ParticleCone2DEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;

	// 底辺の線分から開き角の範囲で上向きに飛ばす
	position = Vector3(RandomGenerator::Generate(-settings.cone.radius, settings.cone.radius), 0.0f, 0.0f);
	const float tilt = RandomGenerator::Generate(
		-settings.cone.angle * degToRad, settings.cone.angle * degToRad);
	direction = Vector3(std::sin(tilt), std::cos(tilt), 0.0f);
}
