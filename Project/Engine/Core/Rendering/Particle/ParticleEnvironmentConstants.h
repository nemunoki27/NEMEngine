#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <cstdint>

namespace Engine {

	// 粒子と不透明描画で共有する環境光の入力
	struct ParticleEnvironmentConstants {

		Vector4 colorIntensity = Vector4(1, 1, 1, 1);
		uint32_t irradianceIndex = UINT32_MAX;
		uint32_t specularIndex = UINT32_MAX;
		uint32_t brdfIndex = UINT32_MAX;
		uint32_t physicalEnvironment = 0;
		float ambientIntensity = 0.03f;
		uint32_t reserved[3]{};
	};
	static_assert(sizeof(ParticleEnvironmentConstants) == 48);
}
