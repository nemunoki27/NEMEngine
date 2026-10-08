#include "MeshNormalMatrixUtility.h"

//============================================================================
//	include
//============================================================================
// c++
#include <cmath>

//============================================================================
//	MeshNormalMatrixUtility functions
//============================================================================
namespace {

	// 線形部の3x3行列式を計算
	float Calc3x3Determinant(const Engine::Matrix4x4& m) {

		return
			m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) -
			m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) +
			m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
	}

	// 全要素が有限か確認
	bool IsMatrixFinite(const Engine::Matrix4x4& m) {

		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				if (!std::isfinite(m.m[i][j])) {
					return false;
				}
			}
		}
		return true;
	}

	// 平行移動と射影成分を除去
	Engine::Matrix4x4 ExtractLinearPart(const Engine::Matrix4x4& m) {

		Engine::Matrix4x4 linear = m;
		linear.m[3][0] = 0.0f;
		linear.m[3][1] = 0.0f;
		linear.m[3][2] = 0.0f;
		linear.m[0][3] = 0.0f;
		linear.m[1][3] = 0.0f;
		linear.m[2][3] = 0.0f;
		linear.m[3][3] = 1.0f;
		return linear;
	}
}

Engine::MeshNormalMatrixResult Engine::BuildSafeMeshNormalMatrix(const Matrix4x4& transform) {

	MeshNormalMatrixResult result{};

	// 非有限の入力には単位行列を使用
	if (!IsMatrixFinite(transform)) {

		result.matrix = Matrix4x4::Identity();
		result.orientationSign = 1.0f;
		result.usedFallback = true;
		return result;
	}

	const float det = Calc3x3Determinant(transform);

	// 行列式から従法線の向きを決定
	result.orientationSign = (det < 0.0f) ? -1.0f : 1.0f;

	// 退化した線形部の逆行列計算を避ける
	constexpr float kDeterminantEpsilon = 1e-8f;
	if (!std::isfinite(det) || std::abs(det) <= kDeterminantEpsilon) {

		// 代替の線形行列を使用
		result.matrix = ExtractLinearPart(transform);
		result.usedFallback = true;
		return result;
	}

	// 法線用の逆転置行列を計算
	const Matrix4x4 normalMatrix = Matrix4x4::Transpose(Matrix4x4::Inverse(transform));

	// 逆行列の計算結果を確認
	if (!IsMatrixFinite(normalMatrix)) {

		result.matrix = ExtractLinearPart(transform);
		result.usedFallback = true;
		return result;
	}

	result.matrix = normalMatrix;
	return result;
}
