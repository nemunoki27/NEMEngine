#include "ScreenSpaceOutlineComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineConstants.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	ScreenSpaceOutlineComponent classMethods
//============================================================================
namespace {

	float SanitizeWidthPixels(float value, float fallback) {

		if (!std::isfinite(value)) {
			return fallback;
		}
		return std::clamp(value, 0.0f,
			static_cast<float>(Engine::kMaxScreenSpaceOutlineRadiusPixels));
	}

	bool TryReadFloat(const nlohmann::json& in, float& outValue) {

		if (!in.is_number()) {
			return false;
		}
		outValue = in.get<float>();
		return std::isfinite(outValue);
	}

	bool TryReadColor4(const nlohmann::json& in, Engine::Color4& outColor) {

		if (in.is_array() && in.size() == 4) {

			Engine::Color4 parsed{};
			if (!TryReadFloat(in[0], parsed.r) ||
				!TryReadFloat(in[1], parsed.g) ||
				!TryReadFloat(in[2], parsed.b) ||
				!TryReadFloat(in[3], parsed.a)) {
				return false;
			}
			outColor = parsed;
			return true;
		}
		if (in.is_object() && in.contains("r") && in.contains("g") && in.contains("b")) {

			Engine::Color4 parsed{};
			if (!TryReadFloat(in["r"], parsed.r) ||
				!TryReadFloat(in["g"], parsed.g) ||
				!TryReadFloat(in["b"], parsed.b)) {
				return false;
			}
			if (in.contains("a")) {

				if (!TryReadFloat(in["a"], parsed.a)) {
					return false;
				}
			} else {

				parsed.a = 1.0f;
			}
			outColor = parsed;
			return true;
		}
		return false;
	}
}

void Engine::from_json(const nlohmann::json& in, ScreenSpaceOutlineComponent& component) {

	if (in.contains("enabled") && in["enabled"].is_boolean()) {
		component.enabled = in["enabled"].get<bool>();
	}
	if (in.contains("color")) {
		Color4 parsedColor{};
		if (TryReadColor4(in["color"], parsedColor)) {
			component.color = parsedColor;
		}
	}
	if (in.contains("widthPixels") && in["widthPixels"].is_number()) {
		component.widthPixels = SanitizeWidthPixels(in["widthPixels"].get<float>(), component.widthPixels);
	}
	if (in.contains("priority") && in["priority"].is_number_integer()) {
		component.priority = in["priority"].get<int32_t>();
	}

	if (in.contains("visibilityMode") && in["visibilityMode"].is_string()) {

		const std::string text = in["visibilityMode"].get<std::string>();
		const auto parsed = EnumAdapter<ScreenSpaceOutlineVisibilityMode>::FromString(text);
		if (parsed) {
			component.visibilityMode = *parsed;
		} else {
			Logger::Output(LogType::Engine,
				"[ScreenSpaceOutline] visibilityMode '{}'が不正なためVisibleOnlyを使用します", text);
		}
	}
	if (in.contains("regionMode") && in["regionMode"].is_string()) {

		const std::string text = in["regionMode"].get<std::string>();
		const auto parsed = EnumAdapter<ScreenSpaceOutlineRegionMode>::FromString(text);
		if (parsed) {
			component.regionMode = *parsed;
		} else {
			Logger::Output(LogType::Engine,
				"[ScreenSpaceOutline] regionMode '{}'は未対応のためAllVisibleSilhouettesを使用します", text);
		}
	}
	if (in.contains("alphaSource") && in["alphaSource"].is_string()) {

		const std::string text = in["alphaSource"].get<std::string>();
		const auto parsed = EnumAdapter<ScreenSpaceOutlineAlphaSource>::FromString(text);
		if (parsed) {
			component.alphaSource = *parsed;
		} else {
			Logger::Output(LogType::Engine,
				"[ScreenSpaceOutline] alphaSource '{}'は未対応のためOutputColorを使用します", text);
		}
	}
	if (in.contains("uiOcclusionMode") && in["uiOcclusionMode"].is_string()) {

		const std::string text = in["uiOcclusionMode"].get<std::string>();
		const auto parsed = EnumAdapter<ScreenSpaceOutlineUIOcclusionMode>::FromString(text);
		if (parsed) {
			component.uiOcclusionMode = *parsed;
		} else {
			Logger::Output(LogType::Engine,
				"[ScreenSpaceOutline] uiOcclusionMode '{}'は未対応のためRespectRenderOrderを使用します", text);
		}
	}
}

void Engine::to_json(nlohmann::json& out, const ScreenSpaceOutlineComponent& component) {

	out["enabled"] = component.enabled;
	out["color"] = component.color.ToJson();
	out["widthPixels"] = SanitizeWidthPixels(component.widthPixels, ScreenSpaceOutlineComponent{}.widthPixels);
	out["priority"] = component.priority;
	out["visibilityMode"] = EnumAdapter<ScreenSpaceOutlineVisibilityMode>::ToString(component.visibilityMode);
	out["regionMode"] = EnumAdapter<ScreenSpaceOutlineRegionMode>::ToString(component.regionMode);
	out["alphaSource"] = EnumAdapter<ScreenSpaceOutlineAlphaSource>::ToString(component.alphaSource);
	out["uiOcclusionMode"] =
		EnumAdapter<ScreenSpaceOutlineUIOcclusionMode>::ToString(component.uiOcclusionMode);
}
