#include "MeshNormalMatrixValidation.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

//============================================================================
//	MeshNormalMatrixValidation
//============================================================================

namespace {

	// row-vector規約での方向ベクトル変換。HLSLの mul(v, (float3x3)m) と同じ
	Engine::Vector3 TransformDir(const Engine::Vector3& v, const Engine::Matrix4x4& m) {

		return Engine::Vector3(
			v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0],
			v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
			v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2]);
	}

	bool IsFiniteMatrix(const Engine::Matrix4x4& m) {

		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				if (!std::isfinite(m.m[i][j])) {
					return false;
				}
			}
		}
		return true;
	}

	// 検証用の集計
	struct ValidationContext {

		int checks = 0;
		int failed = 0;
	};

	void Check(ValidationContext& ctx, bool condition, const char* name) {

		++ctx.checks;
		if (!condition) {
			++ctx.failed;
			Engine::Logger::Output(Engine::LogType::Engine,
				"[MeshNormalMatrixValidate] FAILED: {}", name);
		}
	}

	// 変換後の法線と接線が直交するか(非一様/負スケールでの法線正しさの代理指標)
	bool TransformedTangentNormalOrthogonal(const Engine::Matrix4x4& transform,
		const Engine::Vector3& localNormal, const Engine::Vector3& localTangent) {

		const Engine::MeshNormalMatrixResult result = Engine::BuildSafeMeshNormalMatrix(transform);
		const Engine::Vector3 worldNormal = TransformDir(localNormal, result.matrix).Normalize();
		const Engine::Vector3 worldTangent = TransformDir(localTangent, transform).Normalize();
		return std::abs(Engine::Vector3::Dot(worldNormal, worldTangent)) <= 1e-3f;
	}

	bool IsValidationRequested() {

		// 既存のenv-varデバッグ(NEM_PP_PARAM_DEBUG等)と同じく_dupenv_sで読む
		char* value = nullptr;
		size_t length = 0;
		if (_dupenv_s(&value, &length, "NEM_MESH_NORMAL_MATRIX_VALIDATE") != 0 || value == nullptr) {
			return false;
		}
		const std::string text(value);
		std::free(value);
		return text == "1" || text == "true" || text == "TRUE";
	}
}

bool Engine::RunMeshNormalMatrixValidationIfRequested() {

	if (!IsValidationRequested()) {
		return true;
	}

	ValidationContext ctx{};

	// identity
	{
		const MeshNormalMatrixResult r = BuildSafeMeshNormalMatrix(Matrix4x4::Identity());
		Check(ctx, !r.usedFallback, "identity.noFallback");
		Check(ctx, r.orientationSign == 1.0f, "identity.sign");
		Check(ctx, IsFiniteMatrix(r.matrix), "identity.finite");
	}

	// translation only
	{
		const Matrix4x4 m = Matrix4x4::MakeTranslateMatrix(Vector3(5.0f, -3.0f, 2.0f));
		const MeshNormalMatrixResult r = BuildSafeMeshNormalMatrix(m);
		const Vector3 n = TransformDir(Vector3(0.0f, 1.0f, 0.0f), r.matrix);
		Check(ctx, !r.usedFallback, "translation.noFallback");
		Check(ctx, std::abs(n.y - 1.0f) <= 1e-4f && std::abs(n.x) <= 1e-4f, "translation.normalUnchanged");
		Check(ctx, r.orientationSign == 1.0f, "translation.sign");
	}

	// rotation only
	{
		const Matrix4x4 m = Matrix4x4::MakeRotateMatrix(Vector3(30.0f, 45.0f, 60.0f));
		const MeshNormalMatrixResult r = BuildSafeMeshNormalMatrix(m);
		// 回転のみなら normalMatrix は回転と一致する
		const Vector3 byNormal = TransformDir(Vector3(0.0f, 0.0f, 1.0f), r.matrix);
		const Vector3 byWorld = TransformDir(Vector3(0.0f, 0.0f, 1.0f), m);
		const Vector3 diff = byNormal - byWorld;
		Check(ctx, !r.usedFallback, "rotation.noFallback");
		Check(ctx, diff.Length() <= 1e-3f, "rotation.matchesWorld");
		Check(ctx, r.orientationSign == 1.0f, "rotation.sign");
	}

	// uniform scale
	{
		const Matrix4x4 m = Matrix4x4::MakeScaleMatrix(Vector3(3.0f, 3.0f, 3.0f));
		const MeshNormalMatrixResult r = BuildSafeMeshNormalMatrix(m);
		const Vector3 n = TransformDir(Vector3(0.0f, 1.0f, 0.0f), r.matrix).Normalize();
		Check(ctx, !r.usedFallback, "uniformScale.noFallback");
		Check(ctx, std::abs(n.y - 1.0f) <= 1e-3f, "uniformScale.normalDir");
		Check(ctx, r.orientationSign == 1.0f, "uniformScale.sign");
	}

	// non-uniform scale (4,1,0.25) 直交性
	{
		const Matrix4x4 m = Matrix4x4::MakeScaleMatrix(Vector3(4.0f, 1.0f, 0.25f));
		Check(ctx, TransformedTangentNormalOrthogonal(m, Vector3(0.0f, 0.0f, 1.0f), Vector3(1.0f, 0.0f, 0.0f)),
			"nonUniform.orthogonal");
		const MeshNormalMatrixResult r = BuildSafeMeshNormalMatrix(m);
		Check(ctx, r.orientationSign == 1.0f, "nonUniform.sign");
	}

	// non-uniform + rotation 直交性(斜め法線)
	{
		const Matrix4x4 m = Matrix4x4::MakeAffineMatrix(
			Vector3(0.25f, 3.0f, 1.5f), Vector3(20.0f, 35.0f, 10.0f), Vector3(1.0f, 2.0f, 3.0f));
		Check(ctx, TransformedTangentNormalOrthogonal(m,
			Vector3(0.0f, 0.0f, 1.0f), Vector3(1.0f, 0.0f, 0.0f)), "nonUniformRot.orthogonal");
		Check(ctx, TransformedTangentNormalOrthogonal(m,
			Vector3(0.577f, 0.577f, 0.577f), Vector3(0.707f, -0.707f, 0.0f)), "nonUniformRot.orthogonalDiagonal");
	}

	// negative X scale -> mirror
	{
		const Matrix4x4 m = Matrix4x4::MakeScaleMatrix(Vector3(-1.0f, 1.0f, 1.0f));
		const MeshNormalMatrixResult r = BuildSafeMeshNormalMatrix(m);
		Check(ctx, r.orientationSign == -1.0f, "negativeX.sign");
		Check(ctx, !r.usedFallback, "negativeX.noFallback");
	}

	// negative XYZ scale -> det負(奇数反転)
	{
		const Matrix4x4 m = Matrix4x4::MakeScaleMatrix(Vector3(-2.0f, -3.0f, -1.5f));
		const MeshNormalMatrixResult r = BuildSafeMeshNormalMatrix(m);
		Check(ctx, r.orientationSign == -1.0f, "negativeXYZ.sign");
		Check(ctx, !r.usedFallback, "negativeXYZ.noFallback");
		Check(ctx, IsFiniteMatrix(r.matrix), "negativeXYZ.finite");
	}

	// zero scale -> fallback, NaN/Infを出さない
	{
		const Matrix4x4 m = Matrix4x4::MakeScaleMatrix(Vector3(0.0f, 1.0f, 1.0f));
		const MeshNormalMatrixResult r = BuildSafeMeshNormalMatrix(m);
		Check(ctx, r.usedFallback, "zeroScale.fallback");
		Check(ctx, IsFiniteMatrix(r.matrix), "zeroScale.finite");
	}

	// near-zero scale(全軸が極小=行列式が極小) -> fallback。
	// 1軸だけ小さいケースは行列式がepsilonを上回れば正しく逆転置できるためfallbackしない
	{
		const Matrix4x4 m = Matrix4x4::MakeScaleMatrix(Vector3(1e-5f, 1e-5f, 1e-5f));
		const MeshNormalMatrixResult r = BuildSafeMeshNormalMatrix(m);
		Check(ctx, r.usedFallback, "nearZeroScale.fallback");
		Check(ctx, IsFiniteMatrix(r.matrix), "nearZeroScale.finite");
	}

	// NaN input -> fallback identity, finite
	{
		Matrix4x4 m = Matrix4x4::Identity();
		m.m[1][1] = std::numeric_limits<float>::quiet_NaN();
		const MeshNormalMatrixResult r = BuildSafeMeshNormalMatrix(m);
		Check(ctx, r.usedFallback, "nan.fallback");
		Check(ctx, IsFiniteMatrix(r.matrix), "nan.finite");
	}

	// Inf input -> fallback, finite
	{
		Matrix4x4 m = Matrix4x4::Identity();
		m.m[2][0] = std::numeric_limits<float>::infinity();
		const MeshNormalMatrixResult r = BuildSafeMeshNormalMatrix(m);
		Check(ctx, r.usedFallback, "inf.fallback");
		Check(ctx, IsFiniteMatrix(r.matrix), "inf.finite");
	}

	const bool success = (ctx.failed == 0);
	Logger::Output(LogType::Engine,
		"[MeshNormalMatrixValidate] success={} checks={} failed={}",
		success ? "true" : "false", ctx.checks, ctx.failed);
	return success;
}
