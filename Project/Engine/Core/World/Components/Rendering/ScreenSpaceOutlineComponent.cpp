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

	// widthPixelsは [0, kMaxScreenSpaceOutlineRadiusPixels] へ収める。
	// 負値は0へ、上限超えは上限へ、NaN/Infはdefaultへ倒す。
	// 巨大半径はDilationのGPU Hang原因になるため、ここで必ず上限を掛ける
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

	// 型不一致で例外を外へ漏らさないよう、各fieldを安全に読む
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

	// enumはstable stringで読む。不正値は安全な既定値へ倒し、明示的に通知する
	if (in.contains("visibilityMode") && in["visibilityMode"].is_string()) {

		const std::string text = in["visibilityMode"].get<std::string>();
		const auto parsed = EnumAdapter<ScreenSpaceOutlineVisibilityMode>::FromString(text);
		if (parsed) {
			component.visibilityMode = *parsed;
		} else {
			Logger::Output(LogType::Engine,
				"[ScreenSpaceOutline] unknown visibilityMode '{}', fallback to VisibleOnly", text);
		}
	}
	if (in.contains("regionMode") && in["regionMode"].is_string()) {

		const std::string text = in["regionMode"].get<std::string>();
		const auto parsed = EnumAdapter<ScreenSpaceOutlineRegionMode>::FromString(text);
		if (parsed) {
			component.regionMode = *parsed;
		} else {
			// 未知の値は安全な既定値AllVisibleSilhouettesへ倒す
			Logger::Output(LogType::Engine,
				"[ScreenSpaceOutline] unsupported regionMode '{}', fallback to AllVisibleSilhouettes", text);
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
}
