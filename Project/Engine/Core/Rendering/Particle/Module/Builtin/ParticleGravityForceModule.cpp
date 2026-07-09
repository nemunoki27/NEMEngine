#include "ParticleGravityForceModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>

//============================================================================
//	ParticleGravityForceModule classMethods
//============================================================================
void Engine::ParticleGravityForceModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("gravity"); it != params.end()) {
		gravity_ = Vector3::FromJson(*it);
	}
	reflectGround_ = params.value("reflectGround", reflectGround_);
	reflectGroundY_ = params.value("reflectGroundY", reflectGroundY_);
	restitution_ = params.value("restitution", restitution_);
}

nlohmann::json Engine::ParticleGravityForceModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["gravity"] = gravity_.ToJson();
	params["reflectGround"] = reflectGround_;
	params["reflectGroundY"] = reflectGroundY_;
	params["restitution"] = restitution_;
	return params;
}

void Engine::ParticleGravityForceModule::OnUpdate(std::span<Particle> alive, float deltaTime) {

	for (Particle& particle : alive) {

		particle.velocity += gravity_ * deltaTime;
		// 地面より下へ潜ったら反射させ、反発係数で減衰する
		if (reflectGround_ && particle.position.y <= reflectGroundY_ && particle.velocity.y < 0.0f) {

			particle.velocity = Vector3::Reflect(particle.velocity, Vector3(0.0f, 1.0f, 0.0f)) * restitution_;
			particle.position.y = reflectGroundY_;
		}
	}
}

bool Engine::ParticleGravityForceModule::DrawImGui() {

	bool changed = false;
	changed |= MyGUI::DragVector3("重力", gravity_, ParticleGui::MakeDragSetting(-1000.0f, 1000.0f)).valueChanged;
	changed |= MyGUI::Checkbox("地面で反射", reflectGround_);
	if (reflectGround_) {

		changed |= MyGUI::DragFloat("地面の高さ", reflectGroundY_, ParticleGui::MakeDragSetting(-10000.0f, 10000.0f)).valueChanged;
		changed |= MyGUI::DragFloat("反発係数", restitution_, ParticleGui::MakeDragSetting(0.0f, 1.0f, 0.005f)).valueChanged;
	}
	return changed;
}
