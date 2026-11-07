#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <cstddef>
#include <cstdint>

namespace Engine {

	// 大気と高さ霧の共通描画定数
	struct HeightFogConstants {

		Matrix4x4 inverseViewProjection = Matrix4x4::Identity();
		Vector4 cameraPosition{};
		Vector4 colorDensity{};
		Vector4 heightParameters{};
		Vector4 distanceParameters{};
		Vector4 directionalColor{};
		Vector4 sunDirection{};
		Vector4 secondaryParameters{};
		Vector4 viewportParameters{};
		uint32_t aerialIndex = UINT32_MAX;
		uint32_t irradianceIndex = UINT32_MAX;
		float aerialMaxDistance = 32000.0f;
		float aerialStrength = 1.0f;
		Vector4 skyContribution{};
	};
	static_assert(sizeof(HeightFogConstants) == 224);
	static_assert(offsetof(HeightFogConstants, aerialIndex) == 192);
	static_assert(offsetof(HeightFogConstants, skyContribution) == 208);
}
