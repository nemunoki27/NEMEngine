#include "ParticleSphereEmitterShape.h"

//============================================================================
//	include
//============================================================================

//============================================================================
//	ParticleSphereEmitterShape classMethods
//============================================================================
void Engine::ParticleSphereEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	settings.sphere.radius = data.value("sphereRadius", settings.sphere.radius);
}

void Engine::ParticleSphereEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["sphereRadius"] = settings.sphere.radius;
}

void Engine::ParticleSphereEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	// 球面上から外向きに飛ばす
	direction = Vector3::Normalize(RandomGenerator::Generate(Vector3::AnyInit(-1.0f), Vector3::AnyInit(1.0f)));
	position = direction * settings.sphere.radius;
}
