#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

namespace Engine {

	//============================================================================
	//	MeshNormalMatrixUtility structures
	//============================================================================

	// 安全に構築した法線変換行列の結果
	struct MeshNormalMatrixResult {

		// 法線方向へ適用する逆転置行列
		Matrix4x4 matrix = Matrix4x4::Identity();
		// 負スケールによる従法線の向き補正
		float orientationSign = 1.0f;
		// 退化行列や非有限値で代替行列を使用したか
		bool usedFallback = false;
	};

	//============================================================================
	//	MeshNormalMatrixUtility functions
	//============================================================================
	// 非有限値と退化行列を避けて法線用の逆転置行列を作る
	MeshNormalMatrixResult BuildSafeMeshNormalMatrix(const Matrix4x4& transform);

}
