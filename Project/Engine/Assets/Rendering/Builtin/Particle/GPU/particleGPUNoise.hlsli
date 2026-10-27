#ifndef NEM_PARTICLE_GPU_NOISE
#define NEM_PARTICLE_GPU_NOISE

// CPUと同じ置換表で格子の勾配を選ぶ
static const uint particleNoisePermutation[256] = {
#include "../../../Common/Noise/noisePermutation.hlsli"
};

uint ParticleNoisePerm(int index) { return particleNoisePermutation[index & 255]; }

float ParticleNoiseGradient(uint hash, float3 position) {
	uint h = hash & 15u;
	float u = h < 8u ? position.x : position.y;
	float v = h < 4u ? position.y : (h == 12u || h == 14u ? position.x : position.z);
	return ((h & 1u) == 0 ? u : -u) + ((h & 2u) == 0 ? v : -v);
}

float3 ParticleNoiseCoordinates(float3 position) {
	// 大きい座標だけCPUと同じ周期へ戻す
	return float3(abs(position.x) >= 2147483648.0f ? fmod(position.x, 256) : position.x,
		abs(position.y) >= 2147483648.0f ? fmod(position.y, 256) : position.y,
		abs(position.z) >= 2147483648.0f ? fmod(position.z, 256) : position.z);
}

float ParticlePerlinNoise(float3 position) {
	if (!all(isfinite(position))) return 0;
	position = ParticleNoiseCoordinates(position);
	int3 cell = (int3) floor(position) & 255;
	float3 p = frac(position);
	float3 t = p * p * p * (p * (p * 6 - 15) + 10);
	int a = ParticleNoisePerm(cell.x) + cell.y;
	int b = ParticleNoisePerm(cell.x + 1) + cell.y;
	int aa = ParticleNoisePerm(a) + cell.z;
	int ab = ParticleNoisePerm(a + 1) + cell.z;
	int ba = ParticleNoisePerm(b) + cell.z;
	int bb = ParticleNoisePerm(b + 1) + cell.z;
	return lerp(lerp(lerp(ParticleNoiseGradient(ParticleNoisePerm(aa), p),
		ParticleNoiseGradient(ParticleNoisePerm(ba), p - float3(1, 0, 0)), t.x),
		lerp(ParticleNoiseGradient(ParticleNoisePerm(ab), p - float3(0, 1, 0)),
		ParticleNoiseGradient(ParticleNoisePerm(bb), p - float3(1, 1, 0)), t.x), t.y),
		lerp(lerp(ParticleNoiseGradient(ParticleNoisePerm(aa + 1), p - float3(0, 0, 1)),
		ParticleNoiseGradient(ParticleNoisePerm(ba + 1), p - float3(1, 0, 1)), t.x),
		lerp(ParticleNoiseGradient(ParticleNoisePerm(ab + 1), p - float3(0, 1, 1)),
		ParticleNoiseGradient(ParticleNoisePerm(bb + 1), p - float3(1, 1, 1)), t.x), t.y), t.z);
}

float3 ParticleNoiseVector(float3 position, float frequency) {
	float3 p = position * frequency;
	return float3(ParticlePerlinNoise(p), ParticlePerlinNoise(p + float3(137.2f, 71.9f, 291.3f)),
		ParticlePerlinNoise(p + float3(511.7f, 353.1f, 97.4f)));
}


float3 ParticlePerlinGradient(float3 position) {
	if (!all(isfinite(position))) return 0;
	position = ParticleNoiseCoordinates(position);
	int3 cell = (int3) floor(position) & 255;
	float3 p = frac(position);
	float3 t = p * p * p * (p * (p * 6 - 15) + 10);
	float3 derivative = 30.0f * p * p * (p * (p - 2) + 1);
	float3 result = 0;
	// ノイズの補間式を解析微分する
	[unroll] for (uint corner = 0; corner < 8; ++corner) {
		uint3 offset = uint3(corner & 1u, (corner >> 1u) & 1u, corner >> 2u);
		uint hash = ParticleNoisePerm(ParticleNoisePerm(ParticleNoisePerm(cell.x + offset.x) + cell.y + offset.y) + cell.z + offset.z);
		float3 gradient = float3(ParticleNoiseGradient(hash, float3(1, 0, 0)),
			ParticleNoiseGradient(hash, float3(0, 1, 0)), ParticleNoiseGradient(hash, float3(0, 0, 1)));
		float value = ParticleNoiseGradient(hash, p - offset);
		float3 w = lerp(1.0f.xxx - t, t, float3(offset));
		float3 d = derivative * (float3(offset) * 2.0f - 1.0f);
		result += gradient * (w.x * w.y * w.z) + float3(d.x * w.y * w.z, w.x * d.y * w.z, w.x * w.y * d.z) * value;
	}
	return result;
}

float3 ParticleCurlNoise(float3 position, float frequency) {
	if (!isfinite(frequency)) return 0;
	float3 p = position * frequency;
	float3 x = ParticlePerlinGradient(p);
	float3 y = ParticlePerlinGradient(p + float3(137.2f, 71.9f, 291.3f));
	float3 z = ParticlePerlinGradient(p + float3(511.7f, 353.1f, 97.4f));
	return float3(z.y - y.z, x.z - z.x, y.x - x.y) * frequency;
}

#endif
