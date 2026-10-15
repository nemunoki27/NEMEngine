#include "BloomSettings.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace {

	// 実行中の値があれば保存値より優先する
	const Engine::MaterialParameterValue* FindValue(const Engine::MaterialParameterSet& values,
		const Engine::MaterialParameterSet* overrides, std::string_view name) {

		const auto id = Engine::MaterialParameterID::FromName(name);
		if (overrides) {
			if (const auto* value = overrides->Find(id)) return value;
		}
		return values.Find(id);
	}

	// 整数で保存された数値も読み、非数値は既定値へ戻す
	float ReadFloat(const Engine::MaterialParameterSet& values, const Engine::MaterialParameterSet* overrides,
		std::string_view name, float fallback, float maximum = 65504.0f) {

		const auto* value = FindValue(values, overrides, name);
		if (!value) return fallback;
		const float number = std::visit([fallback](const auto& v) {
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, float> || std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>) return static_cast<float>(v);
			else return fallback;
		}, value->value);
		return std::isfinite(number) ? std::clamp(number, 0.0f, maximum) : fallback;
	}
}

Engine::BloomSettings Engine::ReadBloomSettings(const MaterialParameterSet& parameters, const MaterialParameterSet* overrides) {

	BloomSettings settings;
	settings.intensity = ReadFloat(parameters, overrides, "intensity", settings.intensity);
	settings.threshold = ReadFloat(parameters, overrides, "threshold", settings.threshold);
	settings.softKnee = ReadFloat(parameters, overrides, "softKnee", settings.softKnee, 1.0f);
	settings.scatter = ReadFloat(parameters, overrides, "scatter", settings.scatter, 1.0f);
	// 色もGPUへ渡す前に非数値と負値を除く
	const auto* value = FindValue(parameters, overrides, "tint");
	if (const auto* color = value ? std::get_if<Color4>(&value->value) : nullptr) {
		const auto channel = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 65504.0f) : 1.0f; };
		settings.tint = Color4(channel(color->r), channel(color->g), channel(color->b), 1.0f);
	}
	return settings;
}

void Engine::WriteBloomSettings(MaterialParameterSet& parameters, const BloomSettings& settings) {

	// 既存のPassパラメータAPIと同じ名前を使う
	parameters.Set("intensity", {settings.intensity});
	parameters.Set("threshold", {settings.threshold});
	parameters.Set("softKnee", {settings.softKnee});
	parameters.Set("scatter", {settings.scatter});
	parameters.Set("tint", {settings.tint});
}

std::vector<Engine::RenderTargetSize> Engine::BuildBloomLevelSizes(uint32_t width, uint32_t height) {

	std::vector<RenderTargetSize> sizes;
	if (width == 0 || height == 0) return sizes;
	sizes.reserve(5);
	// 一時RenderTargetと同じ切り捨てで段階を作る
	for (uint32_t level = 0; level < 5; ++level) {
		width = (std::max)(width / 2, 1u);
		height = (std::max)(height / 2, 1u);
		sizes.push_back({width, height});
		if (width == 1 && height == 1) break;
	}
	return sizes;
}
