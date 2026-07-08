#include "ParticleRotationOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Random/RandomGenerator.h>

//============================================================================
//	ParticleRotationOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleRotationOverLifetimeModule::FromJson(const nlohmann::json& params) {

	initialMin_ = params.value("initialMin", initialMin_);
	initialMax_ = params.value("initialMax", initialMax_);
	speedMin_ = params.value("speedMin", speedMin_);
	speedMax_ = params.value("speedMax", speedMax_);
}

nlohmann::json Engine::ParticleRotationOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["initialMin"] = initialMin_;
	params["initialMax"] = initialMax_;
	params["speedMin"] = speedMin_;
	params["speedMax"] = speedMax_;
	return params;
}

void Engine::ParticleRotationOverLifetimeModule::OnSpawn(std::span<Particle> newborn) {

	for (Particle& particle : newborn) {

		particle.rotation = RandomGenerator::Generate(initialMin_, initialMax_);
		particle.rotationSpeed = RandomGenerator::Generate(speedMin_, speedMax_);
	}
}

void Engine::ParticleRotationOverLifetimeModule::OnUpdate(std::span<Particle> alive, float deltaTime) {

	for (Particle& particle : alive) {
		particle.rotation += particle.rotationSpeed * deltaTime;
	}
}
