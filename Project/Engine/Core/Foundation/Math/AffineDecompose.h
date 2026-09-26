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
	//	行ベクトル規約のアフィン行列をSRTへ分解するための共通ヘルパー
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

	// 3Dアフィン行列を平行移動、回転、拡縮に分解する、スケールが潰れている場合はfalse
	bool DecomposeAffine3D(const Matrix4x4& matrix, Vector3& outPos, Quaternion& outRotation, Vector3& outScale);

	// 親ワールド行列から、スケール/回転の無視フラグを反映した追従用行列を作る、平行移動は常に継承する
	Matrix4x4 BuildParentFollowMatrix(const Matrix4x4& parentWorld, bool ignoreScale, bool ignoreRotation);
} // Engine
