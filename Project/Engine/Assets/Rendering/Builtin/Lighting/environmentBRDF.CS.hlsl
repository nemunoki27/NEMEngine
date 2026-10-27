#include "environmentSampling.hlsli"

cbuffer EnvironmentConstants : register(b0) {

	uint sourceIndex;
	uint size;
	float roughnessUnused;
	uint sampleCount;
};
RWTexture2D<float2> gEnvironmentOutput : register(u0);

// 環境光のSmith遮蔽を積分する
float EnvironmentVisibility(float cosine, float roughness) {

	float k = roughness * roughness * 0.5f;
	return cosine / max(cosine * (1.0f - k) + k, 1e-6f);
}

[numthreads(8, 8, 1)]
void main(uint3 pixel : SV_DispatchThreadID) {

	if (any(pixel.xy >= size)) return;
	float2 uv = (float2(pixel.xy) + 0.5f) / float(size);
	float nDotV = uv.x;
	float roughness = uv.y;
	float3 view = float3(sqrt(saturate(1.0f - nDotV * nDotV)), 0, nDotV);
	float2 integral = 0.0f;
	for (uint index = 0; index < sampleCount; ++index) {
		float3 halfDirection = EnvironmentSampleGGX(EnvironmentHammersley(index, sampleCount), float3(0, 0, 1), roughness);
		float3 light = reflect(-view, halfDirection);
		float nDotL = saturate(light.z);
		if (nDotL <= 0.0f) continue;
		float vDotH = saturate(dot(view, halfDirection));
		float visibility = EnvironmentVisibility(nDotV, roughness) * EnvironmentVisibility(nDotL, roughness);
		visibility *= vDotH / max(halfDirection.z * nDotV, 1e-5f);
		float fresnel = pow(1.0f - vDotH, 5.0f);
		integral += float2(1.0f - fresnel, fresnel) * visibility;
	}
	gEnvironmentOutput[pixel.xy] = integral / float(sampleCount);
}
