#ifndef NEM_DECAL_COMMON_HLSLI
#define NEM_DECAL_COMMON_HLSLI

//============================================================================
//	include
//============================================================================
#include "../Common/descriptorHeapCompatibility.hlsli"
#include "../Common/hexTiling.hlsli"

struct DecalGPUData {

	float4x4 worldToDecal;
	float4 tangentX;
	float4 tangentY;
	float4 direction;
	float4 baseColor;
	float4 emissive;
	float4 surface;
	float4 uvTransform;
	float4 fade;
	uint baseColorTexture;
	uint normalTexture;
	uint metallicRoughnessTexture;
	uint emissiveTexture;
	uint channels;
	uint receiverMask;
	uint graphOffset;
	uint graphCount;
	float4 hexTiling;
};

StructuredBuffer<DecalGPUData> gDecals : register(t0, space5);

struct ParticleImpactDecalGPU {
	DecalGPUData decal;
	float4 ageLifetimeFade;
	uint alive;
	float3 center;
};
StructuredBuffer<ParticleImpactDecalGPU> gGPUImpactDecals : register(t7, space4);
cbuffer ParticleImpactDecalSceneConstants : register(b3, space4) {
	uint gGPUImpactDecalCapacity;
	uint gGPUImpactDecalWordCount;
	uint gGPUImpactDecalSummaryCount;
	uint gGPUImpactDecalPad;
};
StructuredBuffer<uint> gGPUImpactDecalBins : register(t10, space4);
StructuredBuffer<uint> gGPUImpactDecalBinWords : register(t11, space4);

// 同じ空間を同じ候補の列へ対応付ける
uint ParticleImpactDecalBin(int3 cell) {
	return ((asuint(cell.x) * 73856093u) ^ (asuint(cell.y) * 19349663u) ^ (asuint(cell.z) * 83492791u)) & 255u;
}

StructuredBuffer<uint2> gDecalReceiverRanges : register(t1, space5);
StructuredBuffer<uint> gDecalReceiverIndices : register(t2, space5);
cbuffer DecalSceneConstants : register(b0, space5) {

	uint gDecalCount;
	uint gDecalReceiverCount;
	uint2 gDecalScenePad;
};

struct DecalSample {

	float3 color;
	float alpha;
	float3 normal;
	float2 surface;
	float3 emissive;
};

// 投影の密度から通常描画と反射で同じMipを選ぶ
float4 SampleDecalTexture(uint index, SamplerState samplerState, HexTilingData hex, float2 uvFootprint, float4 fallbackValue, bool normalMap = false) {

	if (index == 0xffffffffu) return fallbackValue;
	Texture2D<float4> image = NEM_TEXTURE2D(index);
	uint width, height, mipCount;
	image.GetDimensions(0, width, height, mipCount);
	const float mip = clamp(log2(max(1.0f, max(uvFootprint.x * width, uvFootprint.y * height))),
		0.0f, max(0.0f, float(mipCount) - 1.0f));
	return SampleHexLevel(image, samplerState, hex, mip, normalMap);
}

// 面から外れる軸は安定した接線へ置き換える
float3 DecalTangent(float3 axis, float3 normal) {

	float3 tangent = axis - normal * dot(axis, normal);
	if (dot(tangent, tangent) < 1e-8f) {
		tangent = cross(abs(normal.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0), normal);
	}
	return normalize(tangent);
}

// UV行列の逆変換からテクスチャのU・V方向を投影面へ戻す
float3 DecalNormalToWorld(DecalGPUData decal, float3 originalNormal, float3 sampled) {

	const float4 m = decal.uvTransform;
	const float determinant = m.x * m.w - m.y * m.z;
	if (abs(determinant) < 1e-8f) return originalNormal;
	const float orientation = sign(determinant);
	const float3 xAxis = (decal.tangentX.xyz * m.w + decal.tangentY.xyz * m.y) * orientation;
	const float3 yAxis = (-decal.tangentX.xyz * m.z - decal.tangentY.xyz * m.x) * orientation;
	const float3 tangent = DecalTangent(xAxis, originalNormal);
	float3 bitangent = normalize(cross(originalNormal, tangent));
	bitangent *= dot(bitangent, yAxis) < 0.0f ? -1.0f : 1.0f;
	return normalize(tangent * sampled.x + bitangent * sampled.y + originalNormal * sampled.z);
}

#include "decalGraph.hlsli"

// 未変更の受け取り法線を基準に投影を評価する
bool EvaluateDecal(DecalGPUData decal, float3 worldPosition, float3 originalNormal, float worldFootprint,
	SamplerState samplerState, out DecalSample result) {

	result = (DecalSample) 0;
	float3 local = mul(float4(worldPosition, 1.0f), decal.worldToDecal).xyz;
	if (any(abs(local) > 0.5f.xxx)) return false;
	const float facing = dot(originalNormal, -normalize(decal.direction.xyz));
	const float angle = smoothstep(decal.fade.y, max(decal.fade.x, decal.fade.y + 1e-5f), facing);
	if (angle <= 0.0f) return false;
	const float2 projected = float2(local.x, -local.y) + 0.5f;
	const float4 m = decal.uvTransform;
	const float2 uv = projected.x * m.xy + projected.y * m.zw + decal.fade.zw;
	const float3 dx = float3(decal.worldToDecal[0][0], decal.worldToDecal[1][0], decal.worldToDecal[2][0]);
	const float3 dy = -float3(decal.worldToDecal[0][1], decal.worldToDecal[1][1], decal.worldToDecal[2][1]);
	const float2 footprint = worldFootprint * float2(length(dx * m.x + dy * m.z), length(dx * m.y + dy * m.w));
	HexTilingData hex = BuildHexTiling(uv, decal.hexTiling);
	[branch] if (decal.graphCount != 0u)
		return EvaluateDecalGraph(decal, hex, footprint, worldPosition, originalNormal, angle, samplerState, result);
	const float4 color = SampleDecalTexture(decal.baseColorTexture, samplerState, hex, footprint, 1.0f.xxxx);
	result.alpha = saturate(color.a * decal.baseColor.a * decal.surface.w * angle);
	if (result.alpha <= 0.0f) return false;
	result.color = max(0.0f.xxx, color.rgb * decal.baseColor.rgb);
	result.normal = originalNormal;
	if ((decal.channels & 2u) && decal.normalTexture != 0xffffffffu) {
		float3 sampled = SampleDecalTexture(decal.normalTexture, samplerState, hex, footprint,
			float4(0.5f, 0.5f, 1.0f, 1.0f), true).xyz * 2.0f - 1.0f;
		sampled.xy *= decal.surface.z;
		sampled = normalize(float3(sampled.xy, max(1e-4f, sampled.z)));
		result.normal = DecalNormalToWorld(decal, originalNormal, sampled);
	}
	const float4 surface = SampleDecalTexture(decal.metallicRoughnessTexture, samplerState, hex, footprint, 1.0f.xxxx);
	result.surface = saturate(decal.surface.xy * surface.bg);
	result.emissive = max(0.0f.xxx, decal.emissive.rgb *
		SampleDecalTexture(decal.emissiveTexture, samplerState, hex, footprint, 1.0f.xxxx).rgb);
	return true;
}

// 命中Geometryの候補を順に合成する
void ApplyRaytracingDecals(uint geometryIndex, float3 worldPosition, float worldFootprint, uint layerMask,
	SamplerState samplerState, inout float3 color, inout float3 normal, inout float metallic,
	inout float roughness, inout float3 emissive) {

	const uint2 range = geometryIndex < gDecalReceiverCount ? gDecalReceiverRanges[geometryIndex] : uint2(0, 0);
	const float3 originalNormal = normal;
	for (uint index = 0; index < range.y; ++index) {
		const uint decalIndex = gDecalReceiverIndices[range.x + index];
		if (decalIndex >= gDecalCount) continue;
		const DecalGPUData decal = gDecals[decalIndex];
		if ((decal.receiverMask & layerMask) == 0) continue;
		DecalSample decalSample;
		if (!EvaluateDecal(decal, worldPosition, originalNormal, worldFootprint, samplerState, decalSample)) continue;
		if (decal.channels & 1u) color = lerp(color, decalSample.color, decalSample.alpha);
		if (decal.channels & 2u) normal = lerp(normal, decalSample.normal, decalSample.alpha);
		if (decal.channels & 4u) {
			metallic = lerp(metallic, decalSample.surface.x, decalSample.alpha);
			roughness = lerp(roughness, decalSample.surface.y, decalSample.alpha);
		}
		if (decal.channels & 8u) emissive += decalSample.emissive * decalSample.alpha;
	}
	// 空間候補をslot順に合成して重なりのちらつきを防ぐ
	const uint bin = ParticleImpactDecalBin(int3(floor(worldPosition * 0.5f)));
	for (uint group = 0; group < gGPUImpactDecalSummaryCount; ++group) {
		uint words = gGPUImpactDecalBinWords[bin * gGPUImpactDecalSummaryCount + group] |
			gGPUImpactDecalBinWords[256u * gGPUImpactDecalSummaryCount + group];
		while (words != 0) {
			const uint word = group * 32u + firstbitlow(words);
			words &= words - 1u;
			uint candidates = gGPUImpactDecalBins[bin * gGPUImpactDecalWordCount + word] |
				gGPUImpactDecalBins[256u * gGPUImpactDecalWordCount + word];
			while (candidates != 0) {
				const uint slot = word * 32u + firstbitlow(candidates);
				candidates &= candidates - 1u;
				if (slot >= gGPUImpactDecalCapacity) continue;
				ParticleImpactDecalGPU item = gGPUImpactDecals[slot];
				if (item.alive == 0 || (item.decal.receiverMask & layerMask) == 0) continue;
				DecalSample decalSample;
				if (!EvaluateDecal(item.decal, worldPosition, originalNormal, worldFootprint, samplerState, decalSample)) continue;
				if (item.decal.channels & 1u) color = lerp(color, decalSample.color, decalSample.alpha);
				if (item.decal.channels & 2u) normal = lerp(normal, decalSample.normal, decalSample.alpha);
				if (item.decal.channels & 4u) {
					metallic = lerp(metallic, decalSample.surface.x, decalSample.alpha);
					roughness = lerp(roughness, decalSample.surface.y, decalSample.alpha);
				}
				if (item.decal.channels & 8u) emissive += decalSample.emissive * decalSample.alpha;
			}
		}
	}
	normal = normalize(normal);
}
#endif
