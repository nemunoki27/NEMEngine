#include "MeshNormalMatrixUtility.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
// c++
#include <cmath>

//============================================================================
//	MeshNormalMatrixUtility functions
//============================================================================
namespace {

	// 線形部の左上3x3の行列式で行列式は転置で不変なのでrow/columnの添字順は問わない
	float Calc3x3Determinant(const Engine::Matrix4x4& m) {

		return
			m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) -
			m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0]) +
			m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
	}

	// 全要素が有限か(NaN/Infを含まないか)
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

	// 平行移動成分を落として線形部だけ残しfallback時に法線変換へそのまま流用する
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

	// 入力自体が壊れている場合はIdentityへ倒す
	if (!IsMatrixFinite(transform)) {

		result.matrix = Matrix4x4::Identity();
		result.orientationSign = 1.0f;
		result.usedFallback = true;
		return result;
	}

	const float det = Calc3x3Determinant(transform);

	// 行列式の符号で向きのmirrorを判定し退化時も符号だけは決めておく
	result.orientationSign = (det < 0.0f) ? -1.0f : 1.0f;

	// 0スケールや極小スケールではinverseが発散するため、逆行列を呼ばずfallbackする
	constexpr float kDeterminantEpsilon = 1e-8f;
	if (!std::isfinite(det) || std::abs(det) <= kDeterminantEpsilon) {

		// fallbackは決定的にし線形部をそのまま法線変換へ流用して最低限描画を壊さない
		result.matrix = ExtractLinearPart(transform);
		result.usedFallback = true;
		return result;
	}

	// 通常時はinverse-transposeを法線変換行列とする
	const Matrix4x4 normalMatrix = Matrix4x4::Transpose(Matrix4x4::Inverse(transform));

	// inverseが万一NaN/Infを生んだ場合の保険
	if (!IsMatrixFinite(normalMatrix)) {

		result.matrix = ExtractLinearPart(transform);
		result.usedFallback = true;
		return result;
	}

	result.matrix = normalMatrix;
	return result;
}
