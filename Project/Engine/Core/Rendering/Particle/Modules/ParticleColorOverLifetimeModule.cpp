#include "ParticleColorOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleColorOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleColorOverLifetimeModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("startColor"); it != params.end()) {
		startColor_ = Color4::FromJson(*it);
	}
	if (const auto it = params.find("endColor"); it != params.end()) {
		endColor_ = Color4::FromJson(*it);
	}
	easingType_ = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	useCurve_ = params.value("useCurve", useCurve_);
	if (const auto it = params.find("curveChannels"); it != params.end() && it->is_array()) {

		const size_t count = (std::min)(curve_.channels.size(), it->size());
		for (size_t i = 0; i < count; ++i) {
			from_json((*it)[i], curve_.channels[i]);
		}
	}
}

nlohmann::json Engine::ParticleColorOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["startColor"] = startColor_.ToJson();
	params["endColor"] = endColor_.ToJson();
	params["easingType"] = EnumAdapter<EasingType>::ToString(easingType_);
	params["useCurve"] = useCurve_;
	params["curveChannels"] = nlohmann::json::array();
	for (const CurveChannel& channel : curve_.channels) {
		params["curveChannels"].push_back(channel);
	}
	return params;
}

void Engine::ParticleColorOverLifetimeModule::OnUpdate(std::span<Particle> alive, [[maybe_unused]] float deltaTime) {

	for (Particle& particle : alive) {

		const float progress = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
		// カーブ指定があればカーブを優先し、無ければイージング補間する
		const Color4 color = useCurve_ ? curve_.Evaluate(progress) :
			Color4::Lerp(startColor_, endColor_, EasedValue(easingType_, progress));
		particle.color = color;
	}
}
