#include "ParticleGravityForceModule.h"

//============================================================================
//	ParticleGravityForceModule classMethods
//============================================================================
void Engine::ParticleGravityForceModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("gravity"); it != params.end()) {
		gravity_ = Vector3::FromJson(*it);
	}
}

nlohmann::json Engine::ParticleGravityForceModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["gravity"] = gravity_.ToJson();
	return params;
}

void Engine::ParticleGravityForceModule::OnUpdate(std::span<Particle> alive, float deltaTime) {

	for (Particle& particle : alive) {
		particle.velocity += gravity_ * deltaTime;
	}
}
