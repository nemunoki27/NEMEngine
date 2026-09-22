#include "ParticleNoiseForceModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Noise.h>

//============================================================================
//	ParticleNoiseForceModule classMethods
//============================================================================
void Engine::ParticleNoiseForceModule::FromJson(const nlohmann::json& params) {

	settings_.strength = params.value("strength", settings_.strength);
	settings_.frequency = params.value("frequency", settings_.frequency);
}

nlohmann::json Engine::ParticleNoiseForceModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["strength"] = settings_.strength;
	params["frequency"] = settings_.frequency;
	return params;
}

void Engine::ParticleNoiseForceModule::OnUpdate(Particle& particle, float deltaTime) {

	// 経過時間を混ぜて同じ位置でも力が揺らぐようにする
	const Vector3 samplePos = particle.pos + Vector3::AnyInit(particle.age * 0.5f);
	particle.velocity += Math::PerlinNoiseVector3(samplePos, settings_.frequency) * (settings_.strength * deltaTime);
}
