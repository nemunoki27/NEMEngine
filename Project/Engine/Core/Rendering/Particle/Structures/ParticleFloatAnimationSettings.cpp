#include "ParticleFloatAnimationSettings.h"

#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

void Engine::ParticleFloatAnimation::ReadAnimationSettings(
	const nlohmann::json& in, ParticleFloatAnimationSettings& settings) {

	if (!in.is_object()) {
		return;
	}

	settings.start = in.value("start", settings.start);
	settings.end = in.value("end", settings.end);
	settings.easingType = EnumAdapter<EasingType>::FromString(
		in.value("easingType", EnumAdapter<EasingType>::ToString(settings.easingType)))
		.value_or(settings.easingType);
	if (const auto it = in.find("loop"); it != in.end()) {
		from_json(*it, settings.loop);
	}
	settings.useCurve = in.value("useCurve", settings.useCurve);
	if (const auto it = in.find("curve"); it != in.end() && it->is_object()) {
		from_json(*it, settings.curve.channel);
	}
}

nlohmann::json Engine::ParticleFloatAnimation::WriteAnimationSettings(const ParticleFloatAnimationSettings& settings) {

	nlohmann::json out = nlohmann::json::object();
	out["start"] = settings.start;
	out["end"] = settings.end;
	out["easingType"] = EnumAdapter<EasingType>::ToString(settings.easingType);
	to_json(out["loop"], settings.loop);
	out["useCurve"] = settings.useCurve;
	to_json(out["curve"], settings.curve.channel);
	return out;
}

float Engine::ParticleFloatAnimation::EvaluateAnimation(const ParticleFloatAnimationSettings& settings, float rawT) {

	const float progress = settings.loop.LoopedT(rawT);
	return settings.useCurve ? settings.curve.Evaluate(progress) :
		Math::Lerp(settings.start, settings.end, EasedValue(settings.easingType, progress));
}
