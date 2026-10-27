#include "../Common/descriptorHeapCompatibility.hlsli"
#include "environmentSampling.hlsli"

cbuffer EnvironmentConstants : register(b0) {

	uint sourceIndex;
	uint size;
	float roughness;
	uint sampleCount;
};
RWTexture2DArray<float4> gEnvironmentOutput : register(u0);
SamplerState gSampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 pixel : SV_DispatchThreadID) {

	if (any(pixel.xy >= size)) return;
	TextureCube<float4> source = NEM_TEXTURECUBE(sourceIndex);
	float3 normal = EnvironmentCubeDirection(pixel.z, (float2(pixel.xy) + 0.5f) / float(size));
	if (roughness < 0.001f) {
		gEnvironmentOutput[pixel] = source.SampleLevel(gSampler, normal, 0.0f);
		return;
	}
	uint sourceSize, sourceHeight, sourceMips;
	source.GetDimensions(0, sourceSize, sourceHeight, sourceMips);
	float texelArea = 4.0f * kEnvironmentPI / (6.0f * sourceSize * sourceSize);
	float3 color = 0.0f;
	float weight = 0.0f;
	for (uint index = 0; index < sampleCount; ++index) {
		float3 halfDirection = EnvironmentSampleGGX(EnvironmentHammersley(index, sampleCount), normal, roughness);
		float3 direction = reflect(-normal, halfDirection);
		float nDotL = saturate(dot(normal, direction));
		if (nDotL <= 0.0f) continue;
		// サンプルの立体角から入力Mipを選ぶ
		float nDotH = saturate(dot(normal, halfDirection));
		float alpha = roughness * roughness;
		float denominator = nDotH * nDotH * (alpha * alpha - 1.0f) + 1.0f;
		float distribution = alpha * alpha / max(kEnvironmentPI * denominator * denominator, 1e-8f);
		float pdf = max(distribution * 0.25f, 1e-6f);
		float mip = max(0.5f * log2(1.0f / (sampleCount * pdf * texelArea)), 0.0f);
		color += source.SampleLevel(gSampler, direction, mip).rgb * nDotL;
		weight += nDotL;
	}
	gEnvironmentOutput[pixel] = float4(color / max(weight, 1e-5f), 1.0f);
}
