#include "ParticleNoiseUVModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Noise.h>

//============================================================================
//	ParticleNoiseUVModule classMethods
//============================================================================
void Engine::ParticleNoiseUVModule::FromJson(const nlohmann::json& params) {

	settings_.strength = params.value("strength", settings_.strength);
	settings_.frequency = params.value("frequency", settings_.frequency);
}

nlohmann::json Engine::ParticleNoiseUVModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["strength"] = settings_.strength;
	params["frequency"] = settings_.frequency;
	return params;
}

void Engine::ParticleNoiseUVModule::OnUpdate(Particle& particle, float deltaTime) {

	// 粒子ごとにIDを混ぜて別々の揺れ方にする
	const Vector3 samplePos = Vector3(particle.age, static_cast<float>(particle.id) * 0.37f, 0.0f);
	const Vector3 noise = Math::PerlinNoiseVector3(samplePos, settings_.frequency);
	particle.uvOffset += Vector2(noise.x, noise.y) * (settings_.strength * deltaTime);
}
