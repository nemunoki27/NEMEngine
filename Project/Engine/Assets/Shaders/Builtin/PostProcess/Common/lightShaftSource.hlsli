#include "postProcessCommon.hlsli"
#define NEM_LIGHT_COUNTS_REGISTER b2
#include "../../Lighting/lightShaderData.hlsli"

Texture2D<float> gSourceDepth : register(t1);

// 主平行光源の画面位置と遮蔽を求める
float ResolveLightShaftSource(out float2 sourceUV, out float3 lightColor) {

	sourceUV = 0.5f;
	lightColor = 0.0f;
	if (directionalCount == 0u) return 0.0f;
	DirectionalLight light = gDirectionalLights[0];
	float3 direction = -normalize(light.direction);
	float3 viewDirection = mul(float4(direction, 0.0f), cameraView).xyz;
	float4 clip = mul(float4(viewDirection * cameraFar, 1.0f), cameraProjection);
	if (clip.w <= 0.0f || viewDirection.z <= 0.0f) return 0.0f;
	sourceUV = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
	float edge = min(min(sourceUV.x, sourceUV.y), min(1.0f - sourceUV.x, 1.0f - sourceUV.y));
	float screenFade = smoothstep(-0.1f, 0.08f, edge);
	if (screenFade <= 0.0f) return 0.0f;
	uint width, height;
	gSourceDepth.GetDimensions(width, height);
	float visibility = 0.0f;
	// 光源付近の空を平均して遮蔽境界を滑らかにする
	for (int y = -2; y <= 2; ++y) {
		for (int x = -2; x <= 2; ++x) {
			float2 sampleUV = sourceUV + float2(x, y) * 0.004f;
			int2 samplePixel = clamp(int2(sampleUV * float2(width, height)), 0, int2(width, height) - 1);
			visibility += step(0.99999f, gSourceDepth.Load(int3(samplePixel, 0)));
		}
	}
	lightColor = light.color.rgb * max(light.intensity, 0.0f);
	return screenFade * visibility / 25.0f;
}
