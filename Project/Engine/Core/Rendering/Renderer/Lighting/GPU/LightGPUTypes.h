#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	LightGPUTypes structures
	//	CPU->GPU転送用のライト構造体
	//============================================================================
	// 平行光源
	struct DirectionalLightGPU {

		// 色
		Color4 color = Color4::White();

		// 方向
		Vector3 direction = Vector3(0.0f, -1.0f, 0.0f);
		// 強さ
		float intensity = 1.0f;

		// 影の強さ(0.0=影なし, 1.0=完全に黒)
		float shadowStrength = 1.0f;
		float pad[3] = { 0.0f, 0.0f, 0.0f };
	};
	static_assert(sizeof(DirectionalLightGPU) % 16 == 0, "DirectionalLightGPU must be 16 byte aligned");
	// 点光源
	struct PointLightGPU {

		// 色
		Color4 color = Color4::White();

		// 座標
		Vector3 pos = Vector3::AnyInit(0.0f);
		// 強さ
		float intensity = 1.0f;

		// 半径
		float radius = 8.0f;
		// 減衰
		float decay = 1.0f;
		// 影の強さ(0.0=影なし, 1.0=完全に黒)
		float shadowStrength = 1.0f;
		float pad = 0.0f;
	};
	static_assert(sizeof(PointLightGPU) % 16 == 0, "PointLightGPU must be 16 byte aligned");
	// スポットライト
	struct SpotLightGPU {

		// 色
		Color4 color = Color4::White();

		// 方向
		Vector3 direction = Vector3(0.0f, -1.0f, 0.0f);
		// 強さ
		float intensity = 1.0f;

		// 座標
		Vector3 pos = Vector3::AnyInit(0.0f);
		// 距離
		float distance = 10.0f;

		// 減衰
		float decay = 1.0f;
		// 影響角度のcos、既定はcos60度
		float cosAngle = 0.5f;
		float cosFalloffStart = 1.0f;
		float pad = 0.0f;
	};
	static_assert(sizeof(SpotLightGPU) % 16 == 0, "SpotLightGPU must be 16 byte aligned");
	// ライトの数
	struct LightCountsGPU {

		uint32_t directionalCount = 0;
		uint32_t pointCount = 0;
		uint32_t spotCount = 0;
		uint32_t localCount = 0;
	};
	static_assert(sizeof(LightCountsGPU) % 16 == 0, "LightCountsGPU must be 16 byte aligned");

	// クラスターごとのライトインデックス範囲
	struct LightClusterHeaderGPU {

		uint32_t offset = 0;
		uint32_t count = 0;
	};
	// クラスター分割情報
	struct LightClusterConstantsGPU {

		uint32_t tileCountX = 0;
		uint32_t tileCountY = 0;
		uint32_t zSliceCount = 0;
		uint32_t tileSize = 32;

		float nearClip = 0.1f;
		float farClip = 1000.0f;
		float sliceScale = 0.0f;
		float sliceBias = 0.0f;

		uint32_t clusterCount = 0;
		uint32_t maxLightsPerCluster = 256;
		uint32_t pad[2] = { 0, 0 };
	};
	static_assert(sizeof(LightClusterConstantsGPU) % 16 == 0,
		"LightClusterConstantsGPU must be 16 byte aligned");
}
