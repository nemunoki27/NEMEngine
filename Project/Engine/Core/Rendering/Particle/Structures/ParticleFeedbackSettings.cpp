#include "ParticleFeedbackSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <cmath>

namespace {

	// 不正な編集値を既定値へ戻す
	float Finite(float value, float fallback) {

		return std::isfinite(value) ? value : fallback;
	}

	// 色に不正値を残して実行時Textureへ蓄積しない
	void SanitizeColor(Engine::Color4& color) {

		color.r = std::max(0.0f, Finite(color.r, 1.0f));
		color.g = std::max(0.0f, Finite(color.g, 1.0f));
		color.b = std::max(0.0f, Finite(color.b, 1.0f));
		color.a = std::clamp(Finite(color.a, 1.0f), 0.0f, 1.0f);
	}
}

void Engine::SanitizeParticleFeedbackSettings(ParticleFeedbackSettings& settings) {

	settings.eventCapacity = std::clamp(settings.eventCapacity, 1u, ParticleFeedbackSettings::kMaxEventCapacity);
	if (settings.events.size() > ParticleFeedbackSettings::kMaxRoutes) settings.events.resize(ParticleFeedbackSettings::kMaxRoutes);
	for (auto& event : settings.events) {
		event.count = std::clamp(event.count, 1u, 256u);
		event.time = std::max(0.0f, Finite(event.time, 0.5f));
		event.probability = std::clamp(Finite(event.probability, 1.0f), 0.0f, 1.0f);
	}
	settings.paint.radius = std::clamp(Finite(settings.paint.radius, 0.1f), 0.001f, 1000.0f);
	settings.paint.hardness = std::clamp(Finite(settings.paint.hardness, 0.8f), 0.0f, 1.0f);
	SanitizeColor(settings.paint.color);
	SanitizeColor(settings.decal.color);
	settings.decal.capacity = std::clamp(settings.decal.capacity, 1u, ParticleFeedbackSettings::kMaxDecalCapacity);
	settings.decal.lifetime = std::clamp(Finite(settings.decal.lifetime, 5.0f), 0.001f, 3600.0f);
	settings.decal.fadeDuration = std::clamp(Finite(settings.decal.fadeDuration, 1.0f), 0.0f, settings.decal.lifetime);
	settings.decal.size.x = std::clamp(Finite(settings.decal.size.x, 0.2f), 0.001f, 1000.0f);
	settings.decal.size.y = std::clamp(Finite(settings.decal.size.y, 0.2f), 0.001f, 1000.0f);
	settings.decal.size.z = std::clamp(Finite(settings.decal.size.z, 0.1f), 0.001f, 1000.0f);
	settings.decal.receiverRenderingLayerMask &= 0x00ffffffu;
}

void Engine::from_json(const nlohmann::json& in, ParticleFeedbackSettings& settings) {

	settings.eventCapacity = in.value("eventCapacity", settings.eventCapacity);
	settings.events.clear();
	if (const auto it = in.find("events"); it != in.end() && it->is_array()) {
		for (const auto& item : *it) {
			if (settings.events.size() == ParticleFeedbackSettings::kMaxRoutes) break;
			auto& event = settings.events.emplace_back();
			event.enabled = item.value("enabled", event.enabled);
			event.kind = EnumAdapter<ParticleEventKind>::FromString(item.value("kind", "Collision")).value_or(ParticleEventKind::Collision);
			if (const auto target = item.find("target"); target != item.end()) from_json(*target, event.target);
			event.phase = item.value("phase", event.phase);
			event.count = item.value("count", event.count);
			event.time = item.value("time", event.time);
			event.probability = item.value("probability", event.probability);
			event.inheritPosition = item.value("inheritPosition", event.inheritPosition);
			event.inheritNormal = item.value("inheritNormal", event.inheritNormal);
			event.inheritVelocity = item.value("inheritVelocity", event.inheritVelocity);
			event.inheritColor = item.value("inheritColor", event.inheritColor);
			event.inheritSeed = item.value("inheritSeed", event.inheritSeed);
		}
	}
	if (const auto it = in.find("paint"); it != in.end()) {
		auto& paint = settings.paint;
		paint.enabled = it->value("enabled", paint.enabled);
		paint.radius = it->value("radius", paint.radius);
		paint.hardness = it->value("hardness", paint.hardness);
		if (it->contains("color")) paint.color = Color4::FromJson((*it)["color"]);
	}
	if (const auto it = in.find("decal"); it != in.end()) {
		auto& decal = settings.decal;
		decal.enabled = it->value("enabled", decal.enabled);
		decal.material = ParseAssetID(*it, "material");
		if (it->contains("size")) decal.size = Vector3::FromJson((*it)["size"]);
		if (it->contains("color")) decal.color = Color4::FromJson((*it)["color"]);
		decal.lifetime = it->value("lifetime", decal.lifetime);
		decal.fadeDuration = it->value("fadeDuration", decal.fadeDuration);
		decal.capacity = it->value("capacity", decal.capacity);
		decal.receiverRenderingLayerMask = it->value("receiverRenderingLayerMask", decal.receiverRenderingLayerMask);
	}
	SanitizeParticleFeedbackSettings(settings);
}

void Engine::to_json(nlohmann::json& out, const ParticleFeedbackSettings& settings) {

	out = {{"eventCapacity", settings.eventCapacity}, {"events", nlohmann::json::array()}};
	for (const auto& event : settings.events) {
		out["events"].push_back({{"enabled", event.enabled}, {"kind", EnumAdapter<ParticleEventKind>::ToString(event.kind)},
			{"target", event.target}, {"phase", event.phase}, {"count", event.count}, {"time", event.time},
			{"probability", event.probability}, {"inheritPosition", event.inheritPosition}, {"inheritNormal", event.inheritNormal},
			{"inheritVelocity", event.inheritVelocity}, {"inheritColor", event.inheritColor}, {"inheritSeed", event.inheritSeed}});
	}
	const auto& paint = settings.paint;
	out["paint"] = {{"enabled", paint.enabled}, {"radius", paint.radius}, {"hardness", paint.hardness}, {"color", paint.color.ToJson()}};
	const auto& decal = settings.decal;
	out["decal"] = {{"enabled", decal.enabled}, {"material", ToAssetReferenceJson(decal.material)}, {"size", decal.size.ToJson()},
		{"color", decal.color.ToJson()}, {"lifetime", decal.lifetime}, {"fadeDuration", decal.fadeDuration},
		{"capacity", decal.capacity}, {"receiverRenderingLayerMask", decal.receiverRenderingLayerMask}};
}
