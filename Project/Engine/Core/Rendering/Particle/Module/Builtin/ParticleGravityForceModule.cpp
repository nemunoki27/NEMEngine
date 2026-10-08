#include "ParticleGravityForceModule.h"

//============================================================================
//	include
//============================================================================

//============================================================================
//	ParticleGravityForceModule classMethods
//============================================================================
void Engine::ParticleGravityForceModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("gravity"); it != params.end()) {
		settings_.gravity = Vector3::FromJson(*it);
	}
	settings_.reflectGround = params.value("reflectGround", settings_.reflectGround);
	settings_.reflectGroundY = params.value("reflectGroundY", settings_.reflectGroundY);
	settings_.restitution = params.value("restitution", settings_.restitution);
	settings_.autoGroundEmitter = params.value("autoGroundEmitter", settings_.autoGroundEmitter);
}

nlohmann::json Engine::ParticleGravityForceModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["gravity"] = settings_.gravity.ToJson();
	params["reflectGround"] = settings_.reflectGround;
	params["reflectGroundY"] = settings_.reflectGroundY;
	params["restitution"] = settings_.restitution;
	params["autoGroundEmitter"] = settings_.autoGroundEmitter;
	return params;
}

void Engine::ParticleGravityForceModule::OnUpdate(Particle& particle, float deltaTime) {

	particle.velocity += settings_.gravity * deltaTime;
	// 地面より下へ潜ったら反射させ、反発係数で減衰する
	if (settings_.reflectGround && particle.pos.y <= settings_.reflectGroundY && particle.velocity.y < 0.0f) {

		particle.velocity = Vector3::Reflect(particle.velocity, Vector3(0.0f, 1.0f, 0.0f)) * settings_.restitution;
		particle.pos.y = settings_.reflectGroundY;
	}
}
