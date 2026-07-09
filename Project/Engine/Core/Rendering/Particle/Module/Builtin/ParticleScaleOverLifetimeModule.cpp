#include "ParticleScaleOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	ParticleScaleOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleScaleOverLifetimeModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("startScale"); it != params.end()) { startScale_ = Vector3::FromJson(*it); }
	if (const auto it = params.find("endScale"); it != params.end()) { endScale_ = Vector3::FromJson(*it); }
	easingType_ = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	if (const auto it = params.find("loop"); it != params.end()) { from_json(*it, loop_); }
}

nlohmann::json Engine::ParticleScaleOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["startScale"] = startScale_.ToJson();
	params["endScale"] = endScale_.ToJson();
	params["easingType"] = EnumAdapter<EasingType>::ToString(easingType_);
	to_json(params["loop"], loop_);
	return params;
}

void Engine::ParticleScaleOverLifetimeModule::OnUpdate(std::span<Particle> alive, [[maybe_unused]] float deltaTime) {

	for (Particle& particle : alive) {

		const float progress = loop_.LoopedT(particle.age / particle.lifetime);
		particle.scale = Vector3::Lerp(startScale_, endScale_, EasedValue(easingType_, progress));
	}
}

bool Engine::ParticleScaleOverLifetimeModule::DrawImGui() {

	bool changed = false;
	changed |= MyGUI::DragVector3("開始スケール", startScale_, ParticleGui::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= MyGUI::DragVector3("終了スケール", endScale_, ParticleGui::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= ParticleGui::SelectEasing(easingType_);
	changed |= ParticleGui::DrawLoopSettings(loop_);
	return changed;
}
