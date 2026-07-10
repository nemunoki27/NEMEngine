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

bool Engine::ParticleLookToVelocityModule::DrawImGui() {
	// 調整項目なし
	return false;
}

void Engine::ParticleLookToVelocityModule::OnSpawn([[maybe_unused]] std::span<Particle> newborn) {

	// 進行方向から、回転を設定する
	for (auto& particle : newborn) {

		particle.rotation = Quaternion::LookRotation(particle.velocity.Normalize(), Vector3(0.0f, 1.0f, 0.0f)).Normalize();
	}
}

void Engine::ParticleLookToVelocityModule::OnUpdate(std::span<Particle> alive, [[maybe_unused]] float deltaTime) {

	// 進行方向から、回転を設定する
	for (auto& particle : alive) {

		particle.rotation = Quaternion::LookRotation(particle.velocity.Normalize(), Vector3(0.0f, 1.0f, 0.0f)).Normalize();
	}
}