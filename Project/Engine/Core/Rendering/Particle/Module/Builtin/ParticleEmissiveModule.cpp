#include "ParticleEmissiveModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleEmissiveModule classMethods
//============================================================================
void Engine::ParticleEmissiveModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("startColor"); it != params.end()) { startColor_ = Color3::FromJson(*it); }
	if (const auto it = params.find("endColor"); it != params.end()) { endColor_ = Color3::FromJson(*it); }
	startIntensity_ = params.value("startIntensity", startIntensity_);
	endIntensity_ = params.value("endIntensity", endIntensity_);
	const EasingType legacyEasingType = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	colorEasingType_ = EnumAdapter<EasingType>::FromString(
		params.value("colorEasingType", EnumAdapter<EasingType>::ToString(legacyEasingType)))
		.value_or(legacyEasingType);
	intensityEasingType_ = EnumAdapter<EasingType>::FromString(
		params.value("intensityEasingType", EnumAdapter<EasingType>::ToString(legacyEasingType)))
		.value_or(legacyEasingType);
}

nlohmann::json Engine::ParticleEmissiveModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["startColor"] = startColor_.ToJson();
	params["endColor"] = endColor_.ToJson();
	params["startIntensity"] = startIntensity_;
	params["endIntensity"] = endIntensity_;
	params["colorEasingType"] = EnumAdapter<EasingType>::ToString(colorEasingType_);
	params["intensityEasingType"] = EnumAdapter<EasingType>::ToString(intensityEasingType_);
	return params;
}

void Engine::ParticleEmissiveModule::OnUpdate(
	Particle& particle, [[maybe_unused]] float deltaTime) {

	const float progress = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
	const float colorT = EasedValue(colorEasingType_, progress);
	const float intensityT = EasedValue(intensityEasingType_, progress);
	const Color3 color = Color3::Lerp(startColor_, endColor_, colorT);
	particle.emissive = Vector4(color.r, color.g, color.b,
		Math::Lerp(startIntensity_, endIntensity_, intensityT));
}

bool Engine::ParticleEmissiveModule::DrawImGui() {
#if defined(NEM_EDITOR_UI_ENABLED)

	bool changed = false;
	ImGui::PushID("Color");
	changed |= MyGUI::ColorEdit("開始色", startColor_).valueChanged;
	changed |= MyGUI::ColorEdit("終了色", endColor_).valueChanged;
	changed |= ParticleGui::DrawInterpolationEasing(colorEasingType_);
	ImGui::PopID();

	ImGui::PushID("Intensity");
	changed |= MyGUI::DragFloat("開始強度", startIntensity_, ParticleGui::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= MyGUI::DragFloat("終了強度", endIntensity_, ParticleGui::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= ParticleGui::DrawInterpolationEasing(intensityEasingType_);
	ImGui::PopID();
	return changed;
#else
	return false;
#endif
}
