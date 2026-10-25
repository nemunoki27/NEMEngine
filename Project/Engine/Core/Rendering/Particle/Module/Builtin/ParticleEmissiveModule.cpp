#include "ParticleEmissiveModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleEmissiveModule classMethods
//============================================================================
void Engine::ParticleEmissiveModule::FromJson(const nlohmann::json& params) {

	settings_.useParticleColor = params.value("useParticleColor", true);
	if (const auto it = params.find("startColor"); it != params.end()) { settings_.startColor = Color3::FromJson(*it); }
	if (const auto it = params.find("endColor"); it != params.end()) { settings_.endColor = Color3::FromJson(*it); }
	settings_.startIntensity = params.value("startIntensity", settings_.startIntensity);
	settings_.endIntensity = params.value("endIntensity", settings_.endIntensity);
	const EasingType legacyEasingType = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	settings_.colorEasingType = EnumAdapter<EasingType>::FromString(
		params.value("colorEasingType", EnumAdapter<EasingType>::ToString(legacyEasingType)))
		.value_or(legacyEasingType);
	settings_.intensityEasingType = EnumAdapter<EasingType>::FromString(
		params.value("intensityEasingType", EnumAdapter<EasingType>::ToString(legacyEasingType)))
		.value_or(legacyEasingType);
}

nlohmann::json Engine::ParticleEmissiveModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["useParticleColor"] = settings_.useParticleColor;
	params["startColor"] = settings_.startColor.ToJson();
	params["endColor"] = settings_.endColor.ToJson();
	params["startIntensity"] = settings_.startIntensity;
	params["endIntensity"] = settings_.endIntensity;
	params["colorEasingType"] = EnumAdapter<EasingType>::ToString(settings_.colorEasingType);
	params["intensityEasingType"] = EnumAdapter<EasingType>::ToString(settings_.intensityEasingType);
	return params;
}

void Engine::ParticleEmissiveModule::OnUpdate(Particle& particle, [[maybe_unused]] float deltaTime) {

	const float progress = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
	const float intensityT = EasedValue(settings_.intensityEasingType, progress);
	// 色の参照中も発光強度は独立して補間する
	const Color3 color = settings_.useParticleColor ? Color3(particle.color.r, particle.color.g, particle.color.b) :
		Color3::Lerp(settings_.startColor, settings_.endColor, EasedValue(settings_.colorEasingType, progress));
	particle.emissive = Vector4(color.r, color.g, color.b,
		Math::Lerp(settings_.startIntensity, settings_.endIntensity, intensityT));
}
