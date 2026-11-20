#ifndef NEM_HEX_TILING_HLSLI
#define NEM_HEX_TILING_HLSLI

struct HexTilingData {
	float2 uv;
	float2 tileUV[3];
	float2 rotation[3];
	float3 weights;
	bool enabled;
};

uint HexTileHash(uint value) {
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ (value >> 16);
}

float2 RotateHexUV(float2 p, float2 cs) {
	return float2(cs.x * p.x - cs.y * p.y, cs.y * p.x + cs.x * p.y);
}

HexTilingData BuildHexTiling(float2 uv, float4 settings) {
	HexTilingData hex = (HexTilingData)0;
	hex.uv = uv;
	hex.enabled = settings.x > 0.0f;
	if (!hex.enabled) return hex;

	// 正三角形の頂点を六角タイルの中心として使う
	float2 p = uv / settings.x;
	float2 lattice = float2(p.x - p.y * 0.577350269f, p.y * 1.154700538f);
	int2 cell = int2(floor(lattice));
	float2 f = frac(lattice);
	bool lower = f.x + f.y < 1.0f;
	int2 corners[3] = {cell + (lower ? int2(0,0) : int2(1,1)), cell + int2(1,0), cell + int2(0,1)};
	float3 w = lower ? float3(1.0f-f.x-f.y, f.x, f.y) : float3(f.x+f.y-1.0f, 1.0f-f.y, 1.0f-f.x);
	w *= w;
	w *= w;
	hex.weights = w / dot(w, 1.0f.xxx);
	[unroll] for (uint i = 0; i < 3; ++i) {
		int2 c = corners[i];
		uint hash = HexTileHash(asuint(c.x) ^ HexTileHash(asuint(c.y) ^ asuint(settings.z)));
		float2 offset = float2(hash & 65535u, hash >> 16) / 65536.0f;
		float angle = ((HexTileHash(hash) >> 8) * (2.0f / 16777216.0f) - 1.0f) * settings.y;
		sincos(angle, hex.rotation[i].y, hex.rotation[i].x);
		float2 center = float2(c.x + 0.5f*c.y, 0.866025404f*c.y) * settings.x;
		hex.tileUV[i] = center + RotateHexUV(uv-center, hex.rotation[i]) + offset;
	}
	return hex;
}

float4 HexTileValue(float4 value, float2 cs, bool normalMap) {
	if (normalMap) {
		float2 xy = value.xy * 2.0f - 1.0f;
		float z = sqrt(saturate(1.0f - dot(xy,xy)));
		value.xyz = float3(RotateHexUV(xy, float2(cs.x,-cs.y)) / max(z, 0.001f), 1.0f);
	}
	return value;
}

float4 FinishHexSample(float4 value, bool normalMap) {
	if (normalMap) value.xyz = normalize(value.xyz) * 0.5f + 0.5f;
	return value;
}

float4 SampleHexGrad(Texture2D<float4> tex, SamplerState samp, HexTilingData hex, float2 dx, float2 dy, bool normalMap) {
	if (!hex.enabled) return tex.SampleGrad(samp, hex.uv, dx, dy);
	float4 value = 0;
	[unroll] for (uint i = 0; i < 3; ++i) {
		float4 sampleValue = tex.SampleGrad(samp, hex.tileUV[i], RotateHexUV(dx,hex.rotation[i]), RotateHexUV(dy,hex.rotation[i]));
		value += hex.weights[i] * HexTileValue(sampleValue, hex.rotation[i], normalMap);
	}
	return FinishHexSample(value, normalMap);
}

float4 SampleHexLevel(Texture2D<float4> tex, SamplerState samp, HexTilingData hex, float lod, bool normalMap) {
	if (!hex.enabled) return tex.SampleLevel(samp, hex.uv, lod);
	float4 value = 0;
	[unroll] for (uint i = 0; i < 3; ++i) {
		float4 sampleValue = tex.SampleLevel(samp, hex.tileUV[i], lod);
		value += hex.weights[i] * HexTileValue(sampleValue, hex.rotation[i], normalMap);
	}
	return FinishHexSample(value, normalMap);
}
// 頂点シェーダーではSamplerを追加せずに高さを取得する
float LoadWrappedHeight(Texture2D<float4> texture, int2 dimensions, float2 uv) {
	const float2 position = frac(uv) * dimensions - 0.5f;
	const int2 base = int2(floor(position));
	const int2 lo = (base + dimensions) % dimensions;
	const int2 hi = (lo + 1) % dimensions;
	const float2 blend = frac(position);
	const float top = lerp(texture.Load(int3(lo, 0)).r, texture.Load(int3(hi.x, lo.y, 0)).r, blend.x);
	const float bottom = lerp(texture.Load(int3(lo.x, hi.y, 0)).r, texture.Load(int3(hi, 0)).r, blend.x);
	return lerp(top, bottom, blend.y);
}

float SampleHexDisplacement(Texture2D<float4> texture, HexTilingData hex) {
	uint width, height;
	texture.GetDimensions(width, height);
	const int2 dimensions = int2(max(width, 1u), max(height, 1u));
	if (!hex.enabled) return LoadWrappedHeight(texture, dimensions, hex.uv);

	float heightValue = 0.0f;
	[unroll] for (uint i = 0; i < 3; ++i) {
		heightValue += hex.weights[i] * LoadWrappedHeight(texture, dimensions, hex.tileUV[i]);
	}
	return heightValue;
}
#endif
