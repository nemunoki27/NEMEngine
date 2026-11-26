#pragma once

#include <cstdint>
#include <json.hpp>

namespace Engine {

	// 全Viewで共有する反射と遮蔽の設定
	struct SurfaceLightingSettings {
		bool reflectionEnabled = true;
		float reflectionIntensity = 1.0f;
		float reflectionMaxDistance = 600.0f;
		int32_t reflectionQuality = 2;
		nlohmann::json reflectionParameters = nlohmann::json::object();
		bool ambientOcclusionEnabled = true;
		float ambientOcclusionIntensity = 1.0f;
		float ambientOcclusionRadius = 1.0f;
		int32_t ambientOcclusionQuality = 2;
	};
}
