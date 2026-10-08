#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine {

	//============================================================================
	//	AffineDecompose
	//	行ベクトル行列の拡縮・回転・平行移動を求める
	//============================================================================

	// 行ベクトル規約の回転行列からクォータニオンを取り出す
	Quaternion QuaternionFromRotationMatrixRowVector(const Matrix4x4& rowVectorMatrix);

	enum class AffineDecompositionResult {
		Failed,
		Exact,
		Approximate
	};

	// TRSでの再現とせん断を含む近似を区別する
	AffineDecompositionResult DecomposeAffine3DResult(
		const Matrix4x4& matrix, Vector3& outPos, Quaternion& outRotation, Vector3& outScale);

	// 行列を拡縮・回転・平行移動へ分解し、分解失敗ならfalseを返す
	bool DecomposeAffine3D(const Matrix4x4& matrix, Vector3& outPos, Quaternion& outRotation, Vector3& outScale);

	// 平行移動を継承し、指定した拡縮と回転だけを除く
	Matrix4x4 BuildParentFollowMatrix(const Matrix4x4& parentWorld, bool ignoreScale, bool ignoreRotation);
} // Engine
