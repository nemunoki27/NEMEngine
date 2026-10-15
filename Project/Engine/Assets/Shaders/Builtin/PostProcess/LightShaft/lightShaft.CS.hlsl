#include "../Common/lightShaftSource.hlsli"

cbuffer PostProcessParameters : register(b1) {

	float intensity;
	float density;
	float maxDistance;
	float anisotropy;
	float3 tint;
	uint sampleCount;
};

cbuffer DirectionalShadowConstants : register(b3) {

	float4x4 shadowViewProjections[4];
	float4 shadowCascadeSplits;
	uint shadowAvailable;
	uint shadowLightIndex;
	float shadowDepthBias;
	float _shadowPad;
};
Texture2D<float> gDirectionalShadowMap0 : register(t13);
Texture2D<float> gDirectionalShadowMap1 : register(t14);
Texture2D<float> gDirectionalShadowMap2 : register(t15);
Texture2D<float> gDirectionalShadowMap3 : register(t16);

float ReadShadowDepth(uint cascade, int2 pixel) {

	if (cascade == 0u) return gDirectionalShadowMap0.Load(int3(pixel, 0));
	if (cascade == 1u) return gDirectionalShadowMap1.Load(int3(pixel, 0));
	if (cascade == 2u) return gDirectionalShadowMap2.Load(int3(pixel, 0));
	return gDirectionalShadowMap3.Load(int3(pixel, 0));
}

// 窓枠と壁の遮蔽を同じCascadeから求める
float ResolveScatteringVisibility(float3 position) {

	float viewDepth = mul(float4(position, 1.0f), cameraView).z;
	uint cascade = viewDepth <= shadowCascadeSplits.x ? 0u : viewDepth <= shadowCascadeSplits.y ? 1u :
		viewDepth <= shadowCascadeSplits.z ? 2u : 3u;
	float4 clip = mul(float4(position, 1.0f), shadowViewProjections[cascade]);
	float3 projected = clip.xyz / clip.w;
	float2 uv = projected.xy * float2(0.5f, -0.5f) + 0.5f;
	if (any(uv < 0.0f) || any(uv > 1.0f) || projected.z < 0.0f || projected.z > 1.0f) return 0.0f;
	uint width, height;
	if (cascade == 0u) gDirectionalShadowMap0.GetDimensions(width, height);
	else if (cascade == 1u) gDirectionalShadowMap1.GetDimensions(width, height);
	else if (cascade == 2u) gDirectionalShadowMap2.GetDimensions(width, height);
	else gDirectionalShadowMap3.GetDimensions(width, height);
	float2 coordinate = uv * float2(width, height) - 0.5f;
	int2 base = int2(floor(coordinate));
	float2 weight = frac(coordinate);
	float visibility[4];
	for (uint index = 0u; index < 4u; ++index) {
		int2 pixel = clamp(base + int2(index & 1u, index >> 1u), 0, int2(width, height) - 1);
		visibility[index] = step(projected.z - shadowDepthBias, ReadShadowDepth(cascade, pixel));
	}
	return lerp(lerp(visibility[0], visibility[1], weight.x), lerp(visibility[2], visibility[3], weight.x), weight.y);
}

[numthreads(8, 8, 1)]
void main(uint3 pixel : SV_DispatchThreadID) {

	if (IsOutside(pixel.xy)) return;
	float2 uv = (float2(pixel.xy) + 0.5f) * invResolution;
	float4 source = SampleSource(uv);
	if (shadowAvailable == 0u || shadowLightIndex >= directionalCount || intensity <= 0.0f || density <= 0.0f) {
		gDestColor[pixel.xy] = source;
		return;
	}
	uint depthWidth, depthHeight;
	gSourceDepth.GetDimensions(depthWidth, depthHeight);
	int2 depthPixel = clamp(int2(uv * float2(depthWidth, depthHeight)), 0, int2(depthWidth, depthHeight) - 1);
	float depth = gSourceDepth.Load(int3(depthPixel, 0));
	float4 viewPosition = mul(float4(uv * float2(2, -2) + float2(-1, 1), depth, 1.0f), cameraProjectionInverse);
	viewPosition /= viewPosition.w;
	float3 worldPosition = mul(float4(viewPosition.xyz, 1.0f), cameraViewInverse).xyz;
	float3 ray = worldPosition - cameraWorldPos;
	float distance = length(ray);
	float3 direction = ray / max(distance, 1e-5f);
	float rayLength = min(distance, max(maxDistance, 0.0f));
	DirectionalLight light = gDirectionalLights[shadowLightIndex];
	float cosine = dot(direction, -normalize(light.direction));
	float g = clamp(anisotropy, -0.85f, 0.85f);
	float phase = (1.0f - g * g) / (12.5663706f * pow(max(1.0f + g * g - 2.0f * g * cosine, 0.001f), 1.5f));
	float scattering = 0.0f;
	uint count = clamp(sampleCount, 16u, 128u);
	float stepLength = rayLength / float(count);
	float extinction = max(density, 0.0f);
	// 固定間隔で積分しFrameごとのノイズを発生させない
	for (uint index = 0u; index < count; ++index) {
		float distanceFromCamera = (float(index) + 0.5f) * stepLength;
		float3 position = cameraWorldPos + direction * distanceFromCamera;
		float transmittance = exp(-extinction * distanceFromCamera);
		scattering += ResolveScatteringVisibility(position) * transmittance * (1.0f - exp(-extinction * stepLength));
	}
	// 遮られた空間の色を白い霞で置き換えない
	float3 shaft = light.color.rgb * max(light.intensity, 0.0f) * phase * scattering * max(intensity, 0.0f) * tint;
	gDestColor[pixel.xy] = float4(source.rgb + shaft, source.a);
}
