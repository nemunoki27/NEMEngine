#include "ParticleNoiseForceModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Noise.h>

//============================================================================
//	ParticleNoiseForceModule classMethods
//============================================================================
void Engine::ParticleNoiseForceModule::FromJson(const nlohmann::json& params) {

	strength_ = params.value("strength", strength_);
	frequency_ = params.value("frequency", frequency_);
}

nlohmann::json Engine::ParticleNoiseForceModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["strength"] = strength_;
	params["frequency"] = frequency_;
	return params;
}

void Engine::ParticleNoiseForceModule::OnUpdate(std::span<Particle> alive, float deltaTime) {

	for (Particle& particle : alive) {

		// 経過時間を混ぜて同じ位置でも力が揺らぐようにする
		const Vector3 samplePos = particle.position + Vector3::AnyInit(particle.age * 0.5f);
		particle.velocity += Math::PerlinNoiseVector3(samplePos, frequency_) * (strength_ * deltaTime);
	}
}
