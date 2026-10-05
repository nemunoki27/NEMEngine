#include "FoundationMathTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookFrame.h>

// c++
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace NEMTests {

	static_assert(sizeof(Engine::Vector2) == 8 && offsetof(Engine::Vector2, y) == 4);
	static_assert(sizeof(Engine::Vector3) == 12 && offsetof(Engine::Vector3, z) == 8);
	static_assert(sizeof(Engine::Vector4) == 16 && offsetof(Engine::Vector4, w) == 12);
	static_assert(sizeof(Engine::Quaternion) == 16 && offsetof(Engine::Quaternion, w) == 12);
	static_assert(sizeof(Engine::Color3) == 12 && offsetof(Engine::Color3, b) == 8);
	static_assert(sizeof(Engine::Color4) == 16 && offsetof(Engine::Color4, a) == 12);

	// 行列積と回転演算、配列の座標順を確認する
	bool TestMathContracts() {

		// どの1要素が変わっても等値と不等値の結果を反転する
		const auto original = Engine::Matrix4x4::Identity();
		if (!(original == original) || original != original) {
			return false;
		}
		for (int row = 0; row < 4; ++row) {
			for (int column = 0; column < 4; ++column) {
				auto changed = original;
				changed.m[row][column] += 1.0f;
				if (original == changed || changed == original || !(original != changed) || !(changed != original)) {
					return false;
				}
			}
		}
		auto nonFinite = original;
		nonFinite.m[0][0] = std::numeric_limits<float>::quiet_NaN();
		if (nonFinite == nonFinite || !(nonFinite != nonFinite)) {
			return false;
		}
		const auto matrix = Engine::Matrix4x4::MakeAffineMatrix(
			{2.0f, 3.0f, 4.0f}, Engine::Quaternion::FromEulerDegrees({10.0f, 20.0f, 30.0f}), {5.0f, 6.0f, 7.0f});
		auto multiplied = matrix;
		multiplied *= multiplied;
		if (multiplied != matrix * matrix) {
			return false;
		}
		Engine::Matrix4x4 inverse = Engine::Matrix4x4::Identity();
		if (!Engine::Matrix4x4::TryInverse(matrix, inverse)) {
			return false;
		}
		const auto identity = matrix * inverse;
		for (int row = 0; row < 4; ++row) {
			for (int column = 0; column < 4; ++column) {
				if (std::abs(identity.m[row][column] - (row == column ? 1.0f : 0.0f)) > 0.00001f) {
					return false;
				}
			}
		}
		const auto previous = inverse;
		if (Engine::Matrix4x4::TryInverse({}, inverse) || inverse != previous) {
			return false;
		}
		const Engine::Quaternion value{2.0f, 3.0f, 4.0f, 5.0f};
		const auto restored = value / Engine::Quaternion::Identity();
		if (restored != value || value - 1.0f != Engine::Quaternion{1.0f, 2.0f, 3.0f, 4.0f}) {
			return false;
		}
		if (Engine::Vector3::FromJson(nlohmann::json::array({1, 2, 3})) != Engine::Vector3{1, 2, 3} ||
			Engine::Quaternion::FromJson(nlohmann::json::array({2, 3, 4, 5})) != value ||
			Engine::Quaternion::FromJson(value.ToJson()) != value) {
			return false;
		}
		// 巨大値と逆回転の失敗でNaNを公開しない
		Engine::Quaternion inverseRotation = value;
		if (Engine::Quaternion::TryInverse({0, 0, 0, 0}, inverseRotation) || inverseRotation != value ||
			!Engine::Quaternion::TryInverse({std::numeric_limits<float>::max(), 0, 0, 0}, inverseRotation) ||
			!std::isfinite(inverseRotation.x) || inverseRotation.x >= 0.0f ||
			Engine::Vector3::Normalize({std::numeric_limits<float>::max(), 0, 0}) != Engine::Vector3{1, 0, 0} ||
			Engine::Vector3::Normalize({std::numeric_limits<float>::quiet_NaN(), 0, 0}) != Engine::Vector3{}) {
			return false;
		}
		// 総数が32bitを超えるFlipbookでも末尾の行を選ぶ
		const std::vector<int32_t> tiles(3, (std::numeric_limits<int32_t>::max)());
		const auto lastFrame = Engine::CalcFlipbookFrame(tiles, 3, 1.0f);
		const auto invalidFrame = Engine::CalcFlipbookFrame(tiles, 3, std::numeric_limits<float>::quiet_NaN());
		if (std::abs(lastFrame.uvOffset.y - 2.0f / 3.0f) > 0.00001f || invalidFrame.uvOffset.x != 0.0f ||
			invalidFrame.uvOffset.y != 0.0f) {
			return false;
		}
		const float large = Math::WrapDegree360(std::numeric_limits<float>::max());
		const auto normalized = Engine::Quaternion::Normalize({std::numeric_limits<float>::max(), 0, 0, 0});
		const auto blended = Engine::Quaternion::Lerp({0, 0, 0, 2}, {0, 0, 0, -3}, 0.5f);
		if (normalized != Engine::Quaternion{1, 0, 0, 0} || std::abs(blended.Length() - 1.0f) > 0.00001f ||
			Engine::Quaternion::Normalize({0, 0, 0, 0}) != Engine::Quaternion::Identity()) {
			return false;
		}
		return large >= 0.0f && large < 360.0f && Math::WrapDegree360(-90.0f) == 270.0f &&
			   Math::WrapDegree360(360.0f) == 0.0f && std::isinf(Math::WrapDegree360(std::numeric_limits<float>::infinity())) &&
			   std::isnan(Math::WrapDegree360(std::numeric_limits<float>::quiet_NaN()));
	}

	// EditorとRuntimeで共有する分解が元の行列を再構成できるか確認する
	bool TestAffineDecomposition() {

		for (float angle : {0.0f, 30.0f, 90.0f, 180.0f}) {
			for (float scaleZ : {4.0f, -4.0f}) {
				const auto rotation = Engine::Quaternion::FromEulerDegrees({angle, angle * 0.5f, -angle});
				const auto matrix = Engine::Matrix4x4::MakeAffineMatrix({2.0f, 3.0f, scaleZ}, rotation, {5.0f, 6.0f, 7.0f});
				Engine::Vector3 position;
				Engine::Vector3 scale;
				Engine::Quaternion decomposedRotation;
				if (!Engine::DecomposeAffine3D(matrix, position, decomposedRotation, scale)) {
					return false;
				}
				const auto rebuilt = Engine::Matrix4x4::MakeAffineMatrix(scale, decomposedRotation, position);
				for (size_t row = 0; row < 4; ++row) {
					for (size_t column = 0; column < 4; ++column) {
						if (std::abs(matrix.m[row][column] - rebuilt.m[row][column]) > 0.0001f) {
							return false;
						}
					}
				}
			}
		}
		// 失敗時の出力保持とせん断の近似判定を確認する
		Engine::Vector3 position{1, 2, 3};
		Engine::Vector3 scale{4, 5, 6};
		Engine::Quaternion rotation = Engine::Quaternion::Identity();
		auto matrix = Engine::Matrix4x4::Identity();
		matrix.m[0][0] = std::numeric_limits<float>::quiet_NaN();
		if (Engine::DecomposeAffine3DResult(matrix, position, rotation, scale) != Engine::AffineDecompositionResult::Failed ||
			position != Engine::Vector3{1, 2, 3} || scale != Engine::Vector3{4, 5, 6} ||
			rotation != Engine::Quaternion::Identity()) {
			return false;
		}
		matrix = Engine::Matrix4x4::Identity();
		matrix.m[0][1] = 0.5f;
		return Engine::DecomposeAffine3DResult(matrix, position, rotation, scale) ==
			   Engine::AffineDecompositionResult::Approximate;
	}

}
