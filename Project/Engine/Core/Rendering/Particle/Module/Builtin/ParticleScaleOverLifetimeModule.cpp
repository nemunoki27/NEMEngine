#include "ParticleScaleOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleScaleOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleScaleOverLifetimeModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("startScale"); it != params.end()) { settings_.startScale = Vector3::FromJson(*it); }
	if (const auto it = params.find("endScale"); it != params.end()) { settings_.endScale = Vector3::FromJson(*it); }
	settings_.easingType = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	if (const auto it = params.find("loop"); it != params.end()) { from_json(*it, settings_.loop); }
	settings_.useCurve = params.value("useCurve", settings_.useCurve);
	if (const auto it = params.find("curveChannels"); it != params.end() && it->is_array()) {

		const size_t count = (std::min)(settings_.curve.channels.size(), it->size());
		for (size_t i = 0; i < count; ++i) {
			from_json((*it)[i], settings_.curve.channels[i]);
		}
	}
}

nlohmann::json Engine::ParticleScaleOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["startScale"] = settings_.startScale.ToJson();
	params["endScale"] = settings_.endScale.ToJson();
	params["easingType"] = EnumAdapter<EasingType>::ToString(settings_.easingType);
	to_json(params["loop"], settings_.loop);
	params["useCurve"] = settings_.useCurve;
	params["curveChannels"] = nlohmann::json::array();
	for (const CurveChannel& channel : settings_.curve.channels) {
		params["curveChannels"].push_back(channel);
	}
	return params;
}

void Engine::ParticleScaleOverLifetimeModule::OnUpdate(Particle& particle, [[maybe_unused]] float deltaTime) {

	const float progress = settings_.loop.LoopedT(particle.age / particle.lifetime);
	// カーブ指定があればカーブを優先し、無ければイージング補間する
	particle.scale = settings_.useCurve ? settings_.curve.Evaluate(progress) :
		Vector3::Lerp(settings_.startScale, settings_.endScale, EasedValue(settings_.easingType, progress));
}
