#include "ParticleNoiseUVModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Math/Noise.h>

//============================================================================
//	ParticleNoiseUVModule classMethods
//============================================================================
void Engine::ParticleNoiseUVModule::FromJson(const nlohmann::json& params) {

	strength_ = params.value("strength", strength_);
	frequency_ = params.value("frequency", frequency_);
}

nlohmann::json Engine::ParticleNoiseUVModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["strength"] = strength_;
	params["frequency"] = frequency_;
	return params;
}

void Engine::ParticleNoiseUVModule::OnUpdate(Particle& particle, float deltaTime) {

	// 粒子ごとにIDを混ぜて別々の揺れ方にする
	const Vector3 samplePos = Vector3(particle.age, static_cast<float>(particle.id) * 0.37f, 0.0f);
	const Vector3 noise = Math::PerlinNoiseVector3(samplePos, frequency_);
	particle.uvOffset += Vector2(noise.x, noise.y) * (strength_ * deltaTime);
}

bool Engine::ParticleNoiseUVModule::DrawImGui() {
#if defined(NEM_EDITOR_UI_ENABLED)

	bool changed = false;
	changed |= MyGUI::DragFloat("強さ", strength_, ParticleGui::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= MyGUI::DragFloat("周波数", frequency_, ParticleGui::MakeDragSetting(0.001f, 100.0f)).valueChanged;
	return changed;
#else
	return false;
#endif
}
