#include "ParticleLookToVelocityModule.h"

//============================================================================
//	ParticleLookToVelocityModule classMethods
//============================================================================

void Engine::ParticleLookToVelocityModule::FromJson([[maybe_unused]] const nlohmann::json& params) {
	// 調整項目なし
}

nlohmann::json Engine::ParticleLookToVelocityModule::ToJson() const {
	// 調整項目なし
	return nlohmann::json();
}

void Engine::ParticleLookToVelocityModule::OnSpawn(Particle& particle) {

	OnUpdate(particle, 0.0f);
}

void Engine::ParticleLookToVelocityModule::OnUpdate(Particle& particle, [[maybe_unused]] float deltaTime) {

	// 停止中は最後の向きを保持する
	if (Vector3::Length(particle.velocity) <= 0.000001f) {
		return;
	}
	// 進行方向から回転を設定する
	particle.rotation = Quaternion::LookRotation(
		particle.velocity.Normalize(), Vector3(0.0f, 1.0f, 0.0f)).Normalize();
}
