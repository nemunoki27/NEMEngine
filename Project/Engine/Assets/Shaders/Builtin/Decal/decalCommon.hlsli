#ifndef NEM_DECAL_COMMON_HLSLI
#define NEM_DECAL_COMMON_HLSLI

//============================================================================
//	include
//============================================================================
#include "../Common/descriptorHeapCompatibility.hlsli"

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
	uint2 _pad;
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
float4 SampleDecalTexture(uint index, SamplerState samplerState, float2 uv, float2 uvFootprint, float4 fallbackValue) {

	if (index == 0xffffffffu) return fallbackValue;
	Texture2D<float4> image = NEM_TEXTURE2D(index);
	uint width, height, mipCount;
	image.GetDimensions(0, width, height, mipCount);
	const float mip = clamp(log2(max(1.0f, max(uvFootprint.x * width, uvFootprint.y * height))),
		0.0f, max(0.0f, float(mipCount) - 1.0f));
	return image.SampleLevel(samplerState, uv, mip);
}

// 面から外れる軸は安定した接線へ置き換える
float3 DecalTangent(float3 axis, float3 normal) {

	float3 tangent = axis - normal * dot(axis, normal);
	if (dot(tangent, tangent) < 1e-8f) {
		tangent = cross(abs(normal.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0), normal);
	}
	return normalize(tangent);
}

// 未変更の受け取り法線を基準に投影を評価する
bool EvaluateDecal(DecalGPUData decal, float3 worldPosition, float3 originalNormal, float worldFootprint,
	SamplerState samplerState, out DecalSample result) {

	result = (DecalSample) 0;
	float3 local = mul(float4(worldPosition, 1.0f), decal.worldToDecal).xyz;
	if (any(abs(local) > 0.5f.xxx)) return false;
	const float facing = dot(originalNormal, -normalize(decal.direction.xyz));
	const float angle = smoothstep(decal.fade.y, max(decal.fade.x, decal.fade.y + 1e-5f), facing);
	if (angle <= 0.0f) return false;
	float sine, cosine;
	sincos(decal.fade.z, sine, cosine);
	float2 uv = float2(local.x * cosine - local.y * sine, -(local.x * sine + local.y * cosine)) + 0.5f;
	uv = uv * decal.uvTransform.xy + decal.uvTransform.zw;
	float2 footprint = worldFootprint * float2(length(float3(decal.worldToDecal[0][0],
		decal.worldToDecal[1][0], decal.worldToDecal[2][0])), length(float3(decal.worldToDecal[0][1],
		decal.worldToDecal[1][1], decal.worldToDecal[2][1]))) * abs(decal.uvTransform.xy);
	const float4 color = SampleDecalTexture(decal.baseColorTexture, samplerState, uv, footprint, 1.0f.xxxx);
	result.alpha = saturate(color.a * decal.baseColor.a * decal.surface.w * angle);
	if (result.alpha <= 0.0f) return false;
	result.color = max(0.0f.xxx, color.rgb * decal.baseColor.rgb);
	result.normal = originalNormal;
	if ((decal.channels & 2u) && decal.normalTexture != 0xffffffffu) {
		float3 sampled = SampleDecalTexture(decal.normalTexture, samplerState, uv, footprint,
			float4(0.5f, 0.5f, 1.0f, 1.0f)).xyz * 2.0f - 1.0f;
		sampled.xy *= decal.surface.z;
		sampled = normalize(float3(sampled.xy, max(1e-4f, sampled.z)));
		// UVの回転と反転をNormalにも反映する
		float3 xAxis = decal.tangentX.xyz * cosine - decal.tangentY.xyz * sine;
		float3 yAxis = -(decal.tangentX.xyz * sine + decal.tangentY.xyz * cosine);
		float3 tangent = DecalTangent(xAxis, originalNormal) * (decal.uvTransform.x < 0 ? -1.0f : 1.0f);
		float3 bitangent = normalize(cross(originalNormal, tangent));
		bitangent *= dot(bitangent, yAxis) < 0.0f ? -1.0f : 1.0f;
		bitangent *= decal.uvTransform.y < 0 ? -1.0f : 1.0f;
		result.normal = normalize(tangent * sampled.x + bitangent * sampled.y + originalNormal * sampled.z);
	}
	const float4 surface = SampleDecalTexture(decal.metallicRoughnessTexture, samplerState, uv, footprint, 1.0f.xxxx);
	result.surface = saturate(decal.surface.xy * surface.bg);
	result.emissive = max(0.0f.xxx, decal.emissive.rgb *
		SampleDecalTexture(decal.emissiveTexture, samplerState, uv, footprint, 1.0f.xxxx).rgb);
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
