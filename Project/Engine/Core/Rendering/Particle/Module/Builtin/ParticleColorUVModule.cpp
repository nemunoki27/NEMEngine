#include "ParticleColorUVModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <span>

//============================================================================
//	ParticleColorUVModule internal
//============================================================================
namespace {

	constexpr size_t kVector2ChannelCount = 2;

	// カーブチャンネルを読み込む
	void ReadCurveChannels(const nlohmann::json& in, std::span<Engine::CurveChannel> channels) {

		if (!in.is_array()) {
			return;
		}
		const size_t count = (std::min)(channels.size(), in.size());
		for (size_t i = 0; i < count; ++i) {
			from_json(in[i], channels[i]);
		}
	}

	// カーブチャンネルを書き出す
	nlohmann::json WriteCurveChannels(std::span<const Engine::CurveChannel> channels) {

		nlohmann::json out = nlohmann::json::array();
		for (const Engine::CurveChannel& channel : channels) {
			out.push_back(channel);
		}
		return out;
	}

	// Vector2アニメーション設定を読み込む
	void ReadVector2Animation(const nlohmann::json& in,
		Engine::Vector2& start, Engine::Vector2& end, EasingType& easingType,
		Engine::ParticleLoopSettings& loop, bool& useCurve, Engine::CurveVector3& curve) {

		if (!in.is_object()) {
			return;
		}
		if (const auto it = in.find("start"); it != in.end()) { start = Engine::Vector2::FromJson(*it); }
		if (const auto it = in.find("end"); it != in.end()) { end = Engine::Vector2::FromJson(*it); }
		easingType = Engine::EnumAdapter<EasingType>::FromString(
			in.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
		if (const auto it = in.find("loop"); it != in.end()) { from_json(*it, loop); }
		useCurve = in.value("useCurve", useCurve);
		if (const auto it = in.find("curveChannels"); it != in.end()) {
			ReadCurveChannels(*it, std::span(curve.channels.data(), kVector2ChannelCount));
		}
	}

	// Vector2アニメーション設定を書き出す
	nlohmann::json WriteVector2Animation(const Engine::Vector2& start, const Engine::Vector2& end,
		EasingType easingType, const Engine::ParticleLoopSettings& loop,
		bool useCurve, const Engine::CurveVector3& curve) {

		nlohmann::json out = nlohmann::json::object();
		out["start"] = start.ToJson();
		out["end"] = end.ToJson();
		out["easingType"] = Engine::EnumAdapter<EasingType>::ToString(easingType);
		to_json(out["loop"], loop);
		out["useCurve"] = useCurve;
		out["curveChannels"] = WriteCurveChannels(
			std::span(curve.channels.data(), kVector2ChannelCount));
		return out;
	}

	// floatアニメーション設定を読み込む
	void ReadFloatAnimation(const nlohmann::json& in,
		float& start, float& end, EasingType& easingType,
		Engine::ParticleLoopSettings& loop, bool& useCurve, Engine::CurveFloat& curve) {

		if (!in.is_object()) {
			return;
		}
		start = in.value("start", start);
		end = in.value("end", end);
		easingType = Engine::EnumAdapter<EasingType>::FromString(
			in.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
		if (const auto it = in.find("loop"); it != in.end()) { from_json(*it, loop); }
		useCurve = in.value("useCurve", useCurve);
		if (const auto it = in.find("curve"); it != in.end() && it->is_object()) {
			from_json(*it, curve.channel);
		}
	}

	// floatアニメーション設定を書き出す
	nlohmann::json WriteFloatAnimation(float start, float end, EasingType easingType,
		const Engine::ParticleLoopSettings& loop, bool useCurve, const Engine::CurveFloat& curve) {

		nlohmann::json out = nlohmann::json::object();
		out["start"] = start;
		out["end"] = end;
		out["easingType"] = Engine::EnumAdapter<EasingType>::ToString(easingType);
		to_json(out["loop"], loop);
		out["useCurve"] = useCurve;
		to_json(out["curve"], curve.channel);
		return out;
	}
}

//============================================================================
//	ParticleColorUVModule classMethods
//============================================================================
void Engine::ParticleColorUVModule::FromJson(const nlohmann::json& params) {

	const auto offsetIt = params.find("offset");
	if (offsetIt != params.end() && offsetIt->is_object()) {
		settings_.updateType = EnumAdapter<ParticleUVUpdateType>::FromString(
			offsetIt->value("updateType", "Lerp")).value_or(ParticleUVUpdateType::Lerp);
		if (const auto it = offsetIt->find("scrollSpeed"); it != offsetIt->end()) {
			settings_.scrollSpeed = Vector2::FromJson(*it);
		}
		ReadVector2Animation(*offsetIt, settings_.startOffset, settings_.endOffset, settings_.offsetEasingType,
			settings_.offsetLoop, settings_.useOffsetCurve, settings_.offsetCurve);
	} else {
		settings_.updateType = EnumAdapter<ParticleUVUpdateType>::FromString(
			params.value("updateType", "Lerp")).value_or(ParticleUVUpdateType::Lerp);
		if (const auto it = params.find("startOffset"); it != params.end()) { settings_.startOffset = Vector2::FromJson(*it); }
		if (const auto it = params.find("endOffset"); it != params.end()) { settings_.endOffset = Vector2::FromJson(*it); }
		if (const auto it = params.find("scrollSpeed"); it != params.end()) { settings_.scrollSpeed = Vector2::FromJson(*it); }
		settings_.offsetEasingType = EnumAdapter<EasingType>::FromString(
			params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	}

	const auto scaleIt = params.find("scale");
	if (scaleIt != params.end() && scaleIt->is_object()) {
		ReadVector2Animation(*scaleIt, settings_.startScale, settings_.endScale, settings_.scaleEasingType,
			settings_.scaleLoop, settings_.useScaleCurve, settings_.scaleCurve);
	} else {
		if (const auto it = params.find("startScale"); it != params.end()) { settings_.startScale = Vector2::FromJson(*it); }
		if (const auto it = params.find("endScale"); it != params.end()) { settings_.endScale = Vector2::FromJson(*it); }
		settings_.scaleEasingType = EnumAdapter<EasingType>::FromString(
			params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	}

	if (const auto it = params.find("rotation"); it != params.end() && it->is_object()) {
		ReadFloatAnimation(*it, settings_.startRotation, settings_.endRotation, settings_.rotationEasingType,
			settings_.rotationLoop, settings_.useRotationCurve, settings_.rotationCurve);
		if (const auto pivotIt = it->find("pivot"); pivotIt != it->end()) {
			settings_.pivot = Vector2::FromJson(*pivotIt);
		}
	}
}

nlohmann::json Engine::ParticleColorUVModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["offset"] = WriteVector2Animation(settings_.startOffset, settings_.endOffset, settings_.offsetEasingType,
		settings_.offsetLoop, settings_.useOffsetCurve, settings_.offsetCurve);
	params["offset"]["updateType"] = EnumAdapter<ParticleUVUpdateType>::ToString(settings_.updateType);
	params["offset"]["scrollSpeed"] = settings_.scrollSpeed.ToJson();
	params["scale"] = WriteVector2Animation(settings_.startScale, settings_.endScale, settings_.scaleEasingType,
		settings_.scaleLoop, settings_.useScaleCurve, settings_.scaleCurve);
	params["rotation"] = WriteFloatAnimation(settings_.startRotation, settings_.endRotation, settings_.rotationEasingType,
		settings_.rotationLoop, settings_.useRotationCurve, settings_.rotationCurve);
	params["rotation"]["pivot"] = settings_.pivot.ToJson();
	return params;
}

void Engine::ParticleColorUVModule::OnUpdate(Particle& particle, float deltaTime) {

	const float lifetimeT = particle.age / particle.lifetime;
	const float scaleT = settings_.scaleLoop.LoopedT(lifetimeT);
	const float rotationT = settings_.rotationLoop.LoopedT(lifetimeT);

	if (settings_.updateType == ParticleUVUpdateType::Scroll) {
		particle.uvOffset += settings_.scrollSpeed * deltaTime;
	} else {

		const float offsetT = settings_.offsetLoop.LoopedT(lifetimeT);
		if (settings_.useOffsetCurve) {
			particle.uvOffset = Vector2(
				settings_.offsetCurve.channels[0].Evaluate(offsetT),
				settings_.offsetCurve.channels[1].Evaluate(offsetT));
		} else {
			particle.uvOffset = Vector2::Lerp(settings_.startOffset, settings_.endOffset, EasedValue(settings_.offsetEasingType, offsetT));
		}
	}

	if (settings_.useScaleCurve) {
		particle.uvScale = Vector2(
			settings_.scaleCurve.channels[0].Evaluate(scaleT),
			settings_.scaleCurve.channels[1].Evaluate(scaleT));
	} else {
		particle.uvScale = Vector2::Lerp(settings_.startScale, settings_.endScale, EasedValue(settings_.scaleEasingType, scaleT));
	}

	particle.uvRotation = settings_.useRotationCurve ? settings_.rotationCurve.Evaluate(rotationT) :
		std::lerp(settings_.startRotation, settings_.endRotation, EasedValue(settings_.rotationEasingType, rotationT));
	particle.uvPivot = settings_.pivot;
}
