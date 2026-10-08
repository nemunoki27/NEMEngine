#include "FoundationMathTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Math/Noise.h>
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookFrame.h>

// c++
#include <cmath>
#include <array>
#include <cstddef>
#include <limits>
#include <vector>

namespace NEMTests {

	static_assert(sizeof(Engine::Vector2) == 8 && offsetof(Engine::Vector2, y) == 4);
	static_assert(sizeof(Engine::Vector2I) == 8 && offsetof(Engine::Vector2I, y) == 4);
	static_assert(sizeof(Engine::Vector3) == 12 && offsetof(Engine::Vector3, z) == 8);
	static_assert(sizeof(Engine::Vector3I) == 12 && offsetof(Engine::Vector3I, z) == 8);
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
		// ベクトル保存は整数成分と4成分目も維持する
		const Engine::Vector2 vector2{1.25f, 2.5f};
		const Engine::Vector2I vector2I{3, -4};
		const Engine::Vector3I vector3I{3, -4, 5};
		const Engine::Vector4 vector4{1.25f, 2.5f, 5.0f, 0.75f};
		if (Engine::Vector2::FromJson(vector2.ToJson()) != vector2 ||
			Engine::Vector2I::FromJson(vector2I.ToJson()) != vector2I ||
			Engine::Vector3I::FromJson(vector3I.ToJson()) != vector3I ||
			Engine::Vector4::FromJson(vector4.ToJson()) != vector4 ||
			Engine::Vector2::Normalize({std::numeric_limits<float>::max(), 0}) != Engine::Vector2{1, 0} ||
			Engine::Vector2::Normalize({std::numeric_limits<float>::quiet_NaN(), 0}) != Engine::Vector2{}) {
			return false;
		}
		// 長さの計算で巨大値と微小値を潰さない
		for (const float size : {1e30f, 1e-30f}) {
			const Engine::Vector2 two{size, 0};
			const Engine::Vector3 three{size, 0, 0};
			const Engine::Quaternion four{size, 0, 0, 0};
			if (two.Length() != size || Engine::Vector2::Length(two) != size || three.Length() != size ||
				Engine::Vector3::Length(three) != size || four.Length() != size || Engine::Quaternion::Length(four) != size ||
				(two >= Engine::Vector2{size * 2, 0}) || (three >= Engine::Vector3{size * 2, 0, 0}) ||
				(Engine::Vector4{size, 0, 0, 0} >= Engine::Vector4{size * 2, 0, 0, 0})) {
				return false;
			}
		}
		const float infinity = std::numeric_limits<float>::infinity();
		const float nan = std::numeric_limits<float>::quiet_NaN();
		if (!std::isinf(Engine::Vector3{infinity, 0, 0}.Length()) || !std::isnan(Engine::Quaternion{nan, 0, 0, 0}.Length())) {
			return false;
		}
		static_assert(Math::SquaredLength(3.0f, 4.0f) == 25.0);
		// 共通の行列変換でも前方向と単位回転を維持する
		const float maximum = (std::numeric_limits<float>::max)();
		const std::array<Engine::Vector3, 5> directions{{{1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {1, 2, 3}, {maximum, 0, maximum}}};
		for (const auto& direction : directions) {
			const auto rotation = Engine::Quaternion::LookRotation(direction, {0, maximum, 0});
			const auto forward = Engine::Vector3::TransferNormal({0, 0, 1}, Engine::Quaternion::MakeRotateMatrix(rotation));
			const auto expected = Engine::Vector3::NormalizeOr(direction, {}, 0.0f);
			if (!std::isfinite(rotation.Length()) || std::abs(rotation.Length() - 1.0f) > 0.00001f ||
				(forward - expected).Length() > 0.00001f) {
				return false;
			}
		}
		const auto fromY = Engine::Quaternion::FromToY({100, 100, 0});
		const auto mappedY = Engine::Vector3::TransferNormal({0, 1, 0}, Engine::Quaternion::MakeRotateMatrix(fromY));
		if ((mappedY - Engine::Vector3::Normalize({1, 1, 0})).Length() > 0.00001f ||
			Engine::Quaternion::LookRotation({}, {0, 1, 0}) != Engine::Quaternion::Identity() ||
			Engine::Quaternion::LookRotation({std::numeric_limits<float>::infinity(), 0, 0}, {0, 1, 0}) !=
				Engine::Quaternion::Identity()) {
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
		if (!std::isfinite(Math::MakeContinuousAngleDegrees(maximum, -maximum)) ||
			Math::MakeContinuousAngleDegrees(10.0f, 350.0f) != 370.0f || Math::WrapDegree180(-180.0f) != 180.0f) {
			return false;
		}
		// 巨大な格子座標でも周期と有限値を維持する
		const float noise = Math::PerlinNoise3D(0.125f, -0.25f, 0.5f);
		if (Math::PerlinNoise3D(256.125f, -256.25f, 256.5f) != noise ||
			!std::isfinite(Math::PerlinNoise3D(std::numeric_limits<float>::max(), 0.125f, 0.5f)) ||
			!std::isfinite(Math::PerlinNoise3D((std::numeric_limits<float>::lowest)(), 0.125f, 0.5f)) ||
			Math::PerlinNoise3D(std::numeric_limits<float>::infinity(), 0, 0) != 0.0f ||
			Math::PerlinNoise3D(std::numeric_limits<float>::quiet_NaN(), 0, 0) != 0.0f) {
			return false;
		}
		// 色の成分順と未指定alphaの既定値を維持する
		const Engine::Color3 color3{0.125f, 0.25f, 0.5f};
		const Engine::Color4 color4{0.125f, 0.25f, 0.5f, 0.75f};
		if (Engine::Color3::FromJson(color3.ToJson()) != color3 || Engine::Color4::FromJson(color4.ToJson()) != color4 ||
			Engine::Color3::FromJson(nlohmann::json::array({0.125f, 0.25f, 0.5f})) != color3 ||
			Engine::Color4::FromJson(nlohmann::json::array({0.125f, 0.25f, 0.5f, 0.75f})) != color4 ||
			Engine::Color4::FromJson(color3.ToJson()) != Engine::Color4{color3.r, color3.g, color3.b, 1.0f} ||
			Engine::Color4::FromHex(0xff000080).a != 128.0f / 255.0f) {
			return false;
		}
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
		// 巨大な親スケールを無視しても軸方向を維持する
		auto parent = Engine::Matrix4x4::Identity();
		parent.m[0][0] = 0.0f;
		parent.m[0][1] = 1e30f;
		parent.m[1][0] = -2e30f;
		parent.m[1][1] = 0.0f;
		parent.m[2][2] = 3e30f;
		parent.m[3][0] = 5.0f;
		const auto rotationOnly = Engine::BuildParentFollowMatrix(parent, true, false);
		const auto scaleOnly = Engine::BuildParentFollowMatrix(parent, false, true);
		const auto translationOnly = Engine::BuildParentFollowMatrix(parent, true, true);
		if (rotationOnly.m[0][1] != 1.0f || rotationOnly.m[1][0] != -1.0f || rotationOnly.m[2][2] != 1.0f ||
			scaleOnly.m[0][0] != 1e30f || scaleOnly.m[1][1] != 2e30f || scaleOnly.m[2][2] != 3e30f ||
			translationOnly.m[0][0] != 1.0f || translationOnly.m[1][1] != 1.0f ||
			rotationOnly.GetTranslationValue() != Engine::Vector3{5, 0, 0} ||
			Engine::BuildParentFollowMatrix(parent, false, false) != parent) {
			return false;
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
