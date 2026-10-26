#include "ParticleCurlNoiseModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Noise.h>

void Engine::ParticleCurlNoiseModule::FromJson(const nlohmann::json& params) {

	settings_.strength = params.value("strength", settings_.strength);
	settings_.frequency = params.value("frequency", settings_.frequency);
	if (const auto it = params.find("scrollSpeed"); it != params.end()) settings_.scrollSpeed = Vector3::FromJson(*it);
}

nlohmann::json Engine::ParticleCurlNoiseModule::ToJson() const {

	return {{"strength", settings_.strength}, {"frequency", settings_.frequency}, {"scrollSpeed", settings_.scrollSpeed.ToJson()}};
}

void Engine::ParticleCurlNoiseModule::OnUpdate(Particle& particle, float deltaTime) {

	// 粒子の経過時間で流れを移動する
	const Vector3 position = particle.pos + settings_.scrollSpeed * particle.age;
	particle.velocity += Math::CurlNoiseVector3(position, settings_.frequency) * (settings_.strength * deltaTime);
}
