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

	// 定番のパーミューテーションテーブル、256要素を2周分並べる
	constexpr uint8_t kPermutation[256] = {
		151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
		140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
		247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
		57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
		74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
		60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
		65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
		200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
		52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
		207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
		119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
		129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
		218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
		81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
		184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
		222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
	};

	uint8_t Perm(int32_t index) {

		return kPermutation[index & 255];
	}

	// 5次のスムーズステップで格子間を滑らかにつなぐ
	float Fade(float t) {

		return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
	}

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
//	Noise classMethods
//============================================================================
float Math::PerlinNoise3D(float x, float y, float z) {

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
	return LerpValue(
		LerpValue(
			LerpValue(Grad(Perm(aa), x, y, z), Grad(Perm(ba), x - 1.0f, y, z), u),
			LerpValue(Grad(Perm(ab), x, y - 1.0f, z), Grad(Perm(bb), x - 1.0f, y - 1.0f, z), u), v),
		LerpValue(
			LerpValue(Grad(Perm(aa + 1), x, y, z - 1.0f), Grad(Perm(ba + 1), x - 1.0f, y, z - 1.0f), u),
			LerpValue(Grad(Perm(ab + 1), x, y - 1.0f, z - 1.0f), Grad(Perm(bb + 1), x - 1.0f, y - 1.0f, z - 1.0f), u), v), w);
}

Engine::Vector3 Math::PerlinNoiseVector3(const Engine::Vector3& position, float frequency) {

	const float x = position.x * frequency;
	const float y = position.y * frequency;
	const float z = position.z * frequency;

	// 成分ごとに大きくずらした座標からサンプリングして相関を避ける
	return Engine::Vector3(
		PerlinNoise3D(x, y, z),
		PerlinNoise3D(x + 137.2f, y + 71.9f, z + 291.3f),
		PerlinNoise3D(x + 511.7f, y + 353.1f, z + 97.4f));
}
