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
#if defined(NEM_EDITOR_UI_ENABLED)
	// 調整項目なし
	return false;
#else
	return false;
#endif
}

void Engine::ParticleLookToVelocityModule::OnSpawn(Particle& particle) {

	// 進行方向から、回転を設定する
	particle.rotation = Quaternion::LookRotation(
		particle.velocity.Normalize(), Vector3(0.0f, 1.0f, 0.0f)).Normalize();
}

void Engine::ParticleLookToVelocityModule::OnUpdate(
	Particle& particle, [[maybe_unused]] float deltaTime) {

	// 進行方向から、回転を設定する
	particle.rotation = Quaternion::LookRotation(
		particle.velocity.Normalize(), Vector3(0.0f, 1.0f, 0.0f)).Normalize();
}
