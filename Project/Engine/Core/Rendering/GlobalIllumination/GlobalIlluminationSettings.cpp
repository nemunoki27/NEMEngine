#include "GlobalIlluminationSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <algorithm>
#include <cmath>

Engine::GlobalIlluminationSettings Engine::GlobalIlluminationStorage::Validate(GlobalIlluminationSettings settings) {

	// 不正値を既定値へ戻してGPU投入量を制限
	const GlobalIlluminationSettings defaults{};
	settings.quality = std::min(settings.quality, 2u);
	settings.probeSpacing = std::isfinite(settings.probeSpacing) ? std::clamp(settings.probeSpacing, 0.05f, 1000.0f) : defaults.probeSpacing;
	settings.maxRayDistance = std::isfinite(settings.maxRayDistance) ? std::clamp(settings.maxRayDistance, 0.1f, 100000.0f) : defaults.maxRayDistance;
	settings.updateBudgetMilliseconds = std::isfinite(settings.updateBudgetMilliseconds) ?
		std::clamp(settings.updateBudgetMilliseconds, 0.25f, 8.0f) : defaults.updateBudgetMilliseconds;
	settings.debugMode = std::min(settings.debugMode, 3u);
	return settings;
}

Engine::GlobalIlluminationSettings Engine::GlobalIlluminationStorage::Load() {

	GlobalIlluminationSettings settings{};
	const auto path = RuntimePaths::GetProjectSettingsPath(ConfigPaths::kGlobalIllumination);
	if (!JsonAdapter::Check(path)) return settings;
	const auto data = JsonAdapter::Load(path);
	if (!data.is_object()) return settings;

	// 型が合う設定だけ読み込む
	if (data.contains("enabled") && data["enabled"].is_boolean()) settings.enabled = data["enabled"].get<bool>();
	if (data.contains("quality") && data["quality"].is_number_unsigned()) settings.quality = data["quality"].get<uint32_t>();
	if (data.contains("probeSpacing") && data["probeSpacing"].is_number()) settings.probeSpacing = data["probeSpacing"].get<float>();
	if (data.contains("maxRayDistance") && data["maxRayDistance"].is_number()) settings.maxRayDistance = data["maxRayDistance"].get<float>();
	if (data.contains("updateBudgetMilliseconds") && data["updateBudgetMilliseconds"].is_number()) {
		settings.updateBudgetMilliseconds = data["updateBudgetMilliseconds"].get<float>();
	}
	return Validate(settings);
}

void Engine::GlobalIlluminationStorage::Save(const GlobalIlluminationSettings& settings) {

	// 製品へ引き継ぐProject設定を保存
	const auto value = Validate(settings);
	const nlohmann::json data = {
		{"enabled", value.enabled}, {"quality", value.quality}, {"probeSpacing", value.probeSpacing},
		{"maxRayDistance", value.maxRayDistance}, {"updateBudgetMilliseconds", value.updateBudgetMilliseconds}
	};
	JsonAdapter::Save(RuntimePaths::GetProjectSettingsPath(ConfigPaths::kGlobalIllumination), data);
}
