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
		settings_.startColor = Color4::FromJson(*it);
	}
	if (const auto it = params.find("endColor"); it != params.end()) {
		settings_.endColor = Color4::FromJson(*it);
	}
	settings_.easingType = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	settings_.useCurve = params.value("useCurve", settings_.useCurve);
	if (const auto it = params.find("curveChannels"); it != params.end() && it->is_array()) {

		const size_t count = (std::min)(settings_.curve.channels.size(), it->size());
		for (size_t i = 0; i < count; ++i) {
			from_json((*it)[i], settings_.curve.channels[i]);
		}
	}
	if (const auto it = params.find("loop"); it != params.end()) { from_json(*it, settings_.loop); }
}

nlohmann::json Engine::ParticleColorOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["startColor"] = settings_.startColor.ToJson();
	params["endColor"] = settings_.endColor.ToJson();
	params["easingType"] = EnumAdapter<EasingType>::ToString(settings_.easingType);
	params["useCurve"] = settings_.useCurve;
	params["curveChannels"] = nlohmann::json::array();
	for (const CurveChannel& channel : settings_.curve.channels) {
		params["curveChannels"].push_back(channel);
	}
	to_json(params["loop"], settings_.loop);
	return params;
}

void Engine::ParticleColorOverLifetimeModule::OnUpdate(Particle& particle, [[maybe_unused]] float deltaTime) {

	const float progress = settings_.loop.LoopedT(particle.age / particle.lifetime);
	// カーブ指定があればカーブを優先し、無ければイージング補間する
	const Color4 color = settings_.useCurve ? settings_.curve.Evaluate(progress) :
		Color4::Lerp(settings_.startColor, settings_.endColor, EasedValue(settings_.easingType, progress));
	particle.color = color;
}
