#include "ParticleNoiseForceModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
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

void Engine::ParticleNoiseForceModule::OnUpdate(Particle& particle, float deltaTime) {

	// 経過時間を混ぜて同じ位置でも力が揺らぐようにする
	const Vector3 samplePos = particle.pos + Vector3::AnyInit(particle.age * 0.5f);
	particle.velocity += Math::PerlinNoiseVector3(samplePos, frequency_) * (strength_ * deltaTime);
}

bool Engine::ParticleNoiseForceModule::DrawImGui() {

	bool changed = false;
	changed |= MyGUI::DragFloat("強さ", strength_, ParticleGui::MakeDragSetting(0.0f, 1000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("周波数", frequency_, ParticleGui::MakeDragSetting(0.001f, 100.0f)).valueChanged;
	return changed;
}
