#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Math.h>

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
	};
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
		float pad[2] = { 0.0f, 0.0f };
	};
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
		// 影響角度
		float cosAngle = Math::pi / 3.0f;
		float cosFalloffStart = 1.0f;
		float pad = 0.0f;
	};
	// ライトの数
	struct LightCountsGPU {

		uint32_t directionalCount = 0;
		uint32_t pointCount = 0;
		uint32_t spotCount = 0;
		uint32_t localCount = 0;
	};
}