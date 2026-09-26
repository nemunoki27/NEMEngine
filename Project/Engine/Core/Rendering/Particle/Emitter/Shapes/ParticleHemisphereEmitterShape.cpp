#include "ParticleHemisphereEmitterShape.h"

//============================================================================
//	include
//============================================================================

// c++
#include <cmath>

//============================================================================
//	ParticleHemisphereEmitterShape classMethods
//============================================================================
void Engine::ParticleHemisphereEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	settings.sphere.radius = data.value("sphereRadius", settings.sphere.radius);
}

void Engine::ParticleHemisphereEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["sphereRadius"] = settings.sphere.radius;
}

void Engine::ParticleHemisphereEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	// Y上向きの半球面から外向きに飛ばす
	direction = Vector3::Normalize(RandomGenerator::Generate(Vector3::AnyInit(-1.0f), Vector3::AnyInit(1.0f)));
	direction.y = std::abs(direction.y);
	position = direction * settings.sphere.radius;
}
