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

	startScale_ = params.value("startScale", startScale_);
	endScale_ = params.value("endScale", endScale_);
	easingType_ = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	useCurve_ = params.value("useCurve", useCurve_);
	if (const auto it = params.find("curve"); it != params.end() && it->is_object()) {
		from_json(*it, curve_.channel);
	}
}

nlohmann::json Engine::ParticleSizeOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["startScale"] = startScale_;
	params["endScale"] = endScale_;
	params["easingType"] = EnumAdapter<EasingType>::ToString(easingType_);
	params["useCurve"] = useCurve_;
	params["curve"] = curve_.channel;
	return params;
}

void Engine::ParticleSizeOverLifetimeModule::OnUpdate(std::span<Particle> alive, [[maybe_unused]] float deltaTime) {

	for (Particle& particle : alive) {

		const float progress = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
		// カーブ指定があればカーブを優先し、無ければイージング補間する
		const float scale = useCurve_ ? curve_.Evaluate(progress) :
			Math::Lerp(startScale_, endScale_, EasedValue(easingType_, progress));
		particle.size = scale;
	}
}
