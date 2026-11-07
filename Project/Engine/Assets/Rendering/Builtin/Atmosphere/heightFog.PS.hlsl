#include "heightFog.hlsli"

Texture2D<float> fogSceneDepth : register(t0);

float4 main(float4 position : SV_POSITION) : SV_TARGET0 {

	uint width, height;
	fogSceneDepth.GetDimensions(width, height);
	float2 uv = position.xy / float2(width, height);
	float depth = fogSceneDepth.Load(int3(position.xy, 0));
	float4 world = mul(float4(uv * float2(2, -2) + float2(-1, 1), depth, 1), fogInverseViewProjection);
	world.xyz /= max(abs(world.w), 0.000001f) * (world.w < 0 ? -1 : 1);
	float4 fog = EvaluateSceneFog(world.xyz, uv, depth >= 1.0f);
	float opacity = 1.0f - fog.a;
	return float4(fog.rgb / max(opacity, 0.000001f), opacity);
}
