#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <vector>

namespace Engine::SceneGridLayout {

	// グリッド平面の頂点
	struct GridPoint2D {

		// 平面座標
		float x = 0.0f;
		float z = 0.0f;
	};

	// 隣接する間隔の補間値
	struct GridStepBlend {

		// 下側の間隔
		float minorStep0 = 1.0f;
		// 上側の間隔
		float minorStep1 = 1.0f;
		// 補間率
		float blend = 0.0f;
	};

	// 画面の外周から表示範囲を求める
	bool BuildVisibleGroundPolygon(const Engine::ResolvedCameraView& camera, int samplesPerEdge, float planeY,
		float maxGroundRayDistance, std::vector<GridPoint2D>& outPolygon);
	// 平面上の距離を求める
	float DistanceXZ(const Engine::Vector3& a, const Engine::Vector3& b);
	// 画面幅に合わせて間隔を補間する
	GridStepBlend DetermineMinorStepBlend(const Engine::ResolvedCameraView& camera, uint32_t width, uint32_t height,
		const std::vector<GridPoint2D>& polygon, float planeY, float maxGroundRayDistance, float baseHeightDivisor,
		float baseMinStep, float targetPixelMin, float targetPixelMax);
}
