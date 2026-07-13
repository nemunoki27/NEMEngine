#include "ParticleMaterialStructures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	ParticleMaterialStructures internal
//============================================================================
namespace {

	Engine::Vector4 Vector4FromJson(const nlohmann::json& in, const Engine::Vector4& fallback) {

		if (in.is_array() && 4 <= in.size()) {
			return Engine::Vector4(
				in[0].get<float>(),
				in[1].get<float>(),
				in[2].get<float>(),
				in[3].get<float>());
		}
		if (in.is_object()) {
			return Engine::Vector4(
				in.value("x", fallback.x),
				in.value("y", fallback.y),
				in.value("z", fallback.z),
				in.value("w", fallback.w));
		}
		return fallback;
	}

	nlohmann::json Vector4ToJson(const Engine::Vector4& value) {

		return nlohmann::json::array({ value.x, value.y, value.z, value.w });
	}

	void ReadCurveVector3(const nlohmann::json& in, Engine::CurveVector3& curve) {

		if (!in.is_array()) {
			return;
		}
		const size_t count = (std::min)(curve.channels.size(), in.size());
		for (size_t i = 0; i < count; ++i) {
			from_json(in[i], curve.channels[i]);
		}
	}

	nlohmann::json WriteCurveVector3(const Engine::CurveVector3& curve) {

		nlohmann::json out = nlohmann::json::array();
		for (const Engine::CurveChannel& channel : curve.channels) {
			out.push_back(channel);
		}
		return out;
	}

	void ReadCurveFloat(const nlohmann::json& in, Engine::CurveFloat& curve) {

		if (!in.is_object()) {
			return;
		}
		from_json(in, curve.channel);
	}

	nlohmann::json WriteCurveFloat(const Engine::CurveFloat& curve) {

		nlohmann::json out = nlohmann::json::object();
		to_json(out, curve.channel);
		return out;
	}
}

//============================================================================
//	ParticleMaterialStructures functions
//============================================================================
void Engine::to_json(nlohmann::json& out, const ParticleMaterialAnimatedParameter& value) {

	out = nlohmann::json::object();
	out["mode"] = EnumAdapter<ParticleMaterialParameterMode>::ToString(value.mode);
	out["constant"] = Vector4ToJson(value.constant);
	out["start"] = Vector4ToJson(value.start);
	out["end"] = Vector4ToJson(value.end);
	out["easingType"] = EnumAdapter<EasingType>::ToString(value.easingType);
	out["componentCount"] = value.componentCount;
	out["useCurve"] = value.useCurve;
	out["curve3"] = WriteCurveVector3(value.curve3);
	out["curveW"] = WriteCurveFloat(value.curveW);
	to_json(out["loop"], value.loop);
}

void Engine::from_json(const nlohmann::json& in, ParticleMaterialAnimatedParameter& value) {

	if (!in.is_object()) {
		return;
	}
	value.mode = EnumAdapter<ParticleMaterialParameterMode>::FromString(
		in.value("mode", "Constant")).value_or(ParticleMaterialParameterMode::Constant);
	if (const auto it = in.find("constant"); it != in.end()) { value.constant = Vector4FromJson(*it, value.constant); }
	if (const auto it = in.find("start"); it != in.end()) { value.start = Vector4FromJson(*it, value.start); }
	if (const auto it = in.find("end"); it != in.end()) { value.end = Vector4FromJson(*it, value.end); }
	value.easingType = EnumAdapter<EasingType>::FromString(
		in.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	value.componentCount = std::clamp<uint32_t>(in.value("componentCount", value.componentCount), 1u, 4u);
	value.useCurve = in.value("useCurve", value.useCurve);
	if (const auto it = in.find("curve3"); it != in.end()) { ReadCurveVector3(*it, value.curve3); }
	if (const auto it = in.find("curveW"); it != in.end()) { ReadCurveFloat(*it, value.curveW); }
	if (const auto it = in.find("loop"); it != in.end()) { from_json(*it, value.loop); }
}

void Engine::to_json(nlohmann::json& out, const ParticlePhaseMaterialSettings& value) {

	out = nlohmann::json::object();
	out["baseColorTexture"] = ToAssetReferenceJson(value.baseColorTexture);
	out["textureOverrides"] = nlohmann::json::object();
	for (const auto& [name, texture] : value.textureOverrides) {
		out["textureOverrides"][name] = ToAssetReferenceJson(texture);
	}
}

void Engine::from_json(const nlohmann::json& in, ParticlePhaseMaterialSettings& value) {

	if (!in.is_object()) {
		return;
	}
	value.baseColorTexture = ParseAssetID(in, "baseColorTexture");
	if (const auto it = in.find("textureOverrides"); it != in.end() && it->is_object()) {

		value.textureOverrides.clear();
		for (auto textureIt = it->begin(); textureIt != it->end(); ++textureIt) {
			nlohmann::json textureJson = nlohmann::json::object();
			textureJson["asset"] = textureIt.value();
			const AssetID texture = ParseAssetID(textureJson, "asset");
			if (texture) {
				value.textureOverrides[textureIt.key()] = texture;
			}
		}
	}
}

Engine::Vector4 Engine::EvaluateParticleMaterialParameter(
	const ParticleMaterialAnimatedParameter& parameter, float t) {

	if (parameter.mode == ParticleMaterialParameterMode::Constant) {
		return parameter.constant;
	}
	const float loopedT = parameter.loop.LoopedT(t);
	if (parameter.useCurve) {
		if (parameter.componentCount <= 1) {
			return Vector4(parameter.curveW.Evaluate(loopedT), 0.0f, 0.0f, 0.0f);
		}
		const Vector3 xyz = parameter.curve3.Evaluate(loopedT);
		const float w = parameter.componentCount >= 4 ? parameter.curveW.Evaluate(loopedT) : 0.0f;
		return Vector4(xyz.x, xyz.y, xyz.z, w);
	}
	const float easedT = EasedValue(parameter.easingType, loopedT);
	return Vector4(
		std::lerp(parameter.start.x, parameter.end.x, easedT),
		std::lerp(parameter.start.y, parameter.end.y, easedT),
		std::lerp(parameter.start.z, parameter.end.z, easedT),
		std::lerp(parameter.start.w, parameter.end.w, easedT));
}
