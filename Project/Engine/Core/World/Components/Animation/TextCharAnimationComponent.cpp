#include "TextCharAnimationComponent.h"

#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

#include <cmath>
#include <stdexcept>

bool Engine::IsTextCharTimingEasing(EasingType easing) {

	return (easing >= EasingType::Linear && easing <= EasingType::EaseInCirc) ||
		(easing >= EasingType::EaseOutSine && easing <= EasingType::EaseOutCirc) ||
		(easing >= EasingType::EaseInOutSine && easing <= EasingType::EaseInOutCirc);
}

namespace {

	using Settings = Engine::TextCharAnimationSettings;
	using Component = Engine::TextCharAnimationComponent;
	constexpr std::array kSceneSources{&Settings::translationSource, &Settings::rotationSource, &Settings::scaleSource, &Settings::colorSource};
	constexpr std::array kRuntimeSources{&Component::runtimeTranslationSourceID, &Component::runtimeRotationSourceID,
		&Component::runtimeScaleSourceID, &Component::runtimeColorSourceID};
	constexpr std::array<const char*, 4> kAssetKeys{"translationAsset", "rotationAsset", "scaleAsset", "colorAsset"};

	template<class T>
	T ReadEnum(const nlohmann::json& in, const char* key, T fallback) {

		if (!in.contains(key)) return fallback;
		const auto value = Engine::EnumAdapter<T>::FromString(in.at(key).get<std::string>());
		if (!value) throw std::invalid_argument(key);
		return *value;
	}
}

Engine::SceneEntityReference& Engine::TextCharAnimationSettings::SceneSource(size_t slot) {
	return this->*kSceneSources.at(slot);
}

const Engine::SceneEntityReference& Engine::TextCharAnimationSettings::SceneSource(size_t slot) const {
	return this->*kSceneSources.at(slot);
}

Engine::UUID& Engine::TextCharAnimationComponent::RuntimeSource(size_t slot) {
	return this->*kRuntimeSources.at(slot);
}

const Engine::UUID& Engine::TextCharAnimationComponent::RuntimeSource(size_t slot) const {
	return this->*kRuntimeSources.at(slot);
}

void Engine::from_json(const nlohmann::json& in, TextCharAnimationComponent& component) {

	TextCharAnimationSettings s;
	s.enabled = in.value("enabled", s.enabled);
	s.playOnStart = in.value("playOnStart", s.playOnStart);
	s.playInEditMode = in.value("playInEditMode", s.playInEditMode);
	s.timeSource = in.value("timeSource", s.timeSource);
	s.translationSource = in.value("translationSource", s.translationSource);
	s.rotationSource = in.value("rotationSource", s.rotationSource);
	s.scaleSource = in.value("scaleSource", s.scaleSource);
	s.colorSource = in.value("colorSource", s.colorSource);
	s.interval = in.value("interval", s.interval);
	s.totalDuration = in.value("totalDuration", s.totalDuration);
	s.seed = in.value("seed", s.seed);
	PreserveTimeSourceRuntime(component.timeSource, s.timeSource);
	for (size_t i = 0; i < kAssetKeys.size(); ++i) s.sourceAssets[i] = ParseAssetID(in, kAssetKeys[i]);
	const bool clipOnly = in.value("source", "") == "AnimationClip";
	if (clipOnly) s.legacyClip = ParseAssetID(in, "clip");
	for (size_t i = 0; i < s.sourceAssets.size(); ++i) {
		if (clipOnly || s.sourceAssets[i]) s.SceneSource(i) = {};
	}
	s.order = ReadEnum(in, "order", s.order);
	s.timing = ReadEnum(in, "timing", s.timing);
	s.lines = ReadEnum(in, "lines", s.lines);
	s.repeat = ReadEnum(in, "repeat", s.repeat);
	s.easing = ReadEnum(in, "easing", s.easing);
	if (!IsTextCharTimingEasing(s.easing) || !std::isfinite(s.interval) || s.interval < 0 ||
		!std::isfinite(s.totalDuration) || s.totalDuration < 0) throw std::invalid_argument("文字の開始時間設定が不正です");
	auto changed = [](const SceneEntityReference& a, const SceneEntityReference& b) {
		return a.localFileID != b.localFileID || a.sourceAsset != b.sourceAsset || a.prefab != b.prefab;
	};
	for (size_t i = 0; i < s.sourceAssets.size(); ++i) {
		if (clipOnly || s.sourceAssets[i] || changed(component.SceneSource(i), s.SceneSource(i))) component.RuntimeSource(i) = {};
	}
	static_cast<TextCharAnimationSettings&>(component) = s;
}

void Engine::to_json(nlohmann::json& out, const TextCharAnimationComponent& s) {

	out = {{"enabled", s.enabled},
		{"playOnStart", s.playOnStart},
		{"playInEditMode", s.playInEditMode},
		{"timeSource", s.timeSource},
		{"translationSource", s.translationSource},
		{"rotationSource", s.rotationSource},
		{"scaleSource", s.scaleSource},
		{"colorSource", s.colorSource},
		{"interval", s.interval},
		{"totalDuration", s.totalDuration},
		{"seed", s.seed},
		{"order", EnumAdapter<decltype(s.order)>::ToString(s.order)},
		{"timing", EnumAdapter<decltype(s.timing)>::ToString(s.timing)},
		{"lines", EnumAdapter<decltype(s.lines)>::ToString(s.lines)},
		{"repeat", EnumAdapter<decltype(s.repeat)>::ToString(s.repeat)},
		{"easing", EnumAdapter<decltype(s.easing)>::ToString(s.easing)}};
	for (size_t i = 0; i < kAssetKeys.size(); ++i) out[kAssetKeys[i]] = ToString(s.sourceAssets[i]);
	if (s.legacyClip) {
		out["source"] = "AnimationClip";
		out["clip"] = ToString(s.legacyClip);
	}
}
