#include "Noise.h"

//============================================================================
//	include
//============================================================================

// c++
#include <cmath>
#include <cstdint>

//============================================================================
//	Noise internal
//============================================================================
namespace {

	// 整数の範囲外だけ周期内へ戻す
	float WrapNoiseCoordinate(float value) {

		return value >= 2147483648.0f || value < -2147483648.0f ? std::fmod(value, 256.0f) : value;
	}

	// 256要素の置換表を周期的に参照
	constexpr uint8_t kPermutation[256] = {
#include <Engine/Assets/Shaders/Common/Noise/noisePermutation.hlsli>
	};

	// 格子番号を置換表の範囲へ戻す
	uint8_t Perm(int32_t index) {

		return kPermutation[index & 255];
	}

	// 5次のスムーズステップで格子間を滑らかにつなぐ
	float Fade(float t) {

		return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
	}

	// 両端の勾配値を線形補間する
	float LerpValue(float a, float b, float t) {

		return a + (b - a) * t;
	}

	// ハッシュ値から12方向の勾配と内積を取る
	float Grad(uint8_t hash, float x, float y, float z) {

		const uint8_t h = hash & 15;
		const float u = h < 8 ? x : y;
		const float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
		return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
	}
}

//============================================================================
//	Noise namespaceMethods
//============================================================================
float Math::PerlinNoise3D(float x, float y, float z) {

	// 非有限の座標は中立値として扱う
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
		return 0.0f;
	}
	// 整数の範囲外だけ周期内へ戻して格子番号を求める
	x = WrapNoiseCoordinate(x);
	y = WrapNoiseCoordinate(y);
	z = WrapNoiseCoordinate(z);
	const int32_t cellX = static_cast<int32_t>(std::floor(x)) & 255;
	const int32_t cellY = static_cast<int32_t>(std::floor(y)) & 255;
	const int32_t cellZ = static_cast<int32_t>(std::floor(z)) & 255;

	x -= std::floor(x);
	y -= std::floor(y);
	z -= std::floor(z);

	const float u = Fade(x);
	const float v = Fade(y);
	const float w = Fade(z);

	// 格子8頂点のハッシュを求める
	const int32_t a = Perm(cellX) + cellY;
	const int32_t aa = Perm(a) + cellZ;
	const int32_t ab = Perm(a + 1) + cellZ;
	const int32_t b = Perm(cellX + 1) + cellY;
	const int32_t ba = Perm(b) + cellZ;
	const int32_t bb = Perm(b + 1) + cellZ;

	// 各頂点の勾配との内積を3方向で補間する
	return LerpValue(LerpValue(LerpValue(Grad(Perm(aa), x, y, z), Grad(Perm(ba), x - 1.0f, y, z), u),
						 LerpValue(Grad(Perm(ab), x, y - 1.0f, z), Grad(Perm(bb), x - 1.0f, y - 1.0f, z), u), v),
		LerpValue(LerpValue(Grad(Perm(aa + 1), x, y, z - 1.0f), Grad(Perm(ba + 1), x - 1.0f, y, z - 1.0f), u),
			LerpValue(Grad(Perm(ab + 1), x, y - 1.0f, z - 1.0f), Grad(Perm(bb + 1), x - 1.0f, y - 1.0f, z - 1.0f), u), v),
		w);
}

Engine::Vector3 Math::PerlinNoiseVector3(const Engine::Vector3& position, float frequency) {

	const float x = position.x * frequency;
	const float y = position.y * frequency;
	const float z = position.z * frequency;

	// 成分ごとに大きくずらした座標からサンプリングして相関を避ける
	return Engine::Vector3(PerlinNoise3D(x, y, z), PerlinNoise3D(x + 137.2f, y + 71.9f, z + 291.3f),
		PerlinNoise3D(x + 511.7f, y + 353.1f, z + 97.4f));
}

Engine::Vector3 Math::PerlinNoiseGradient3D(const Engine::Vector3& position) {

	if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return {};
	const float coordinates[]{WrapNoiseCoordinate(position.x), WrapNoiseCoordinate(position.y), WrapNoiseCoordinate(position.z)};
	int32_t cell[3];
	float fraction[3], weight[3], derivative[3];
	for (uint32_t axis = 0; axis < 3; ++axis) {
		const float floor = std::floor(coordinates[axis]);
		cell[axis] = static_cast<int32_t>(floor) & 255;
		fraction[axis] = coordinates[axis] - floor;
		weight[axis] = Fade(fraction[axis]);
		const float t = fraction[axis];
		derivative[axis] = 30.0f * t * t * (t * (t - 2.0f) + 1.0f);
	}
	Engine::Vector3 result{};
	// 8頂点の勾配と補間重みを微分する
	for (uint32_t corner = 0; corner < 8; ++corner) {
		const uint32_t x = corner & 1u, y = (corner >> 1) & 1u, z = corner >> 2;
		const uint8_t hash = Perm(Perm(Perm(cell[0] + x) + cell[1] + y) + cell[2] + z);
		const Engine::Vector3 gradient(Grad(hash, 1, 0, 0), Grad(hash, 0, 1, 0), Grad(hash, 0, 0, 1));
		const float value = Grad(hash, fraction[0] - x, fraction[1] - y, fraction[2] - z);
		const float wx = x ? weight[0] : 1.0f - weight[0];
		const float wy = y ? weight[1] : 1.0f - weight[1];
		const float wz = z ? weight[2] : 1.0f - weight[2];
		result += gradient * (wx * wy * wz) + Engine::Vector3(
			(x ? derivative[0] : -derivative[0]) * wy * wz,
			wx * (y ? derivative[1] : -derivative[1]) * wz,
			wx * wy * (z ? derivative[2] : -derivative[2])) * value;
	}
	return result;
}

Engine::Vector3 Math::CurlNoiseVector3(const Engine::Vector3& position, float frequency) {

	if (!std::isfinite(frequency)) return {};
	// 解析微分を使い、差分法の18回から3回の格子評価へ減らす
	const Engine::Vector3 p = position * frequency;
	const Engine::Vector3 x = PerlinNoiseGradient3D(p);
	const Engine::Vector3 y = PerlinNoiseGradient3D(p + Engine::Vector3(137.2f, 71.9f, 291.3f));
	const Engine::Vector3 z = PerlinNoiseGradient3D(p + Engine::Vector3(511.7f, 353.1f, 97.4f));
	return Engine::Vector3(z.y - y.z, x.z - z.x, y.x - x.y) * frequency;
}
