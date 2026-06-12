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

		// transpose(inverse(transform))で法線方向の変換に使う
		Matrix4x4 matrix = Matrix4x4::Identity();
		// 線形部の行列式の符号で負スケールのmirrorで-1になり従法線の向き補正に使う
		float orientationSign = 1.0f;
		// 退化行列やNaN/Infでfallbackした場合true
		bool usedFallback = false;
	};

	//============================================================================
	//	MeshNormalMatrixUtility functions
	//============================================================================
	// worldMatrix/localMatrixから法線変換行列を安全に構築する
	// 非一様スケールや負スケールでも法線を正しく変換でき、0スケールなどの退化時も
	// NaN/InfをGPUバッファへ送らないようfallbackする
	// HLSL側はmul(localNormal, (float3x3)matrix)で使う前提(row-vector規約)
	MeshNormalMatrixResult BuildSafeMeshNormalMatrix(const Matrix4x4& transform);

} // Engine
