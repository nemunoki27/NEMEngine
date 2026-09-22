#include "ParticleAlphaReferenceModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleAlphaReferenceModule classMethods
//============================================================================
void Engine::ParticleAlphaReferenceModule::FromJson(const nlohmann::json& params) {

	settings_.startReference = params.value("startReference", settings_.startReference);
	settings_.endReference = params.value("endReference", settings_.endReference);
	settings_.easingType = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
}

nlohmann::json Engine::ParticleAlphaReferenceModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["startReference"] = settings_.startReference;
	params["endReference"] = settings_.endReference;
	params["easingType"] = EnumAdapter<EasingType>::ToString(settings_.easingType);
	return params;
}

void Engine::ParticleAlphaReferenceModule::OnUpdate(Particle& particle, [[maybe_unused]] float deltaTime) {

	const float progress = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
	particle.alphaReference = Math::Lerp(settings_.startReference, settings_.endReference, EasedValue(settings_.easingType, progress));
}
