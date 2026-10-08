#include "ParticleSizeOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleSizeOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleSizeOverLifetimeModule::FromJson(const nlohmann::json& params) {

	settings_.startScale = params.value("startScale", settings_.startScale);
	settings_.endScale = params.value("endScale", settings_.endScale);
	settings_.easingType = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	settings_.useCurve = params.value("useCurve", settings_.useCurve);
	if (const auto it = params.find("curve"); it != params.end() && it->is_object()) {
		from_json(*it, settings_.curve.channel);
	}
	if (const auto it = params.find("loop"); it != params.end()) { from_json(*it, settings_.loop); }
}

nlohmann::json Engine::ParticleSizeOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["startScale"] = settings_.startScale;
	params["endScale"] = settings_.endScale;
	params["easingType"] = EnumAdapter<EasingType>::ToString(settings_.easingType);
	params["useCurve"] = settings_.useCurve;
	params["curve"] = settings_.curve.channel;
	to_json(params["loop"], settings_.loop);
	return params;
}

void Engine::ParticleSizeOverLifetimeModule::OnUpdate(Particle& particle, [[maybe_unused]] float deltaTime) {

	const float progress = settings_.loop.LoopedT(particle.age / particle.lifetime);
	// カーブ指定があればカーブを優先し、無ければイージング補間する
	const float scale = settings_.useCurve ? settings_.curve.Evaluate(progress) :
		Math::Lerp(settings_.startScale, settings_.endScale, EasedValue(settings_.easingType, progress));
	particle.size = scale;
}
