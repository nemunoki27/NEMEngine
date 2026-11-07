#ifndef NEM_TRANSPARENT_FOG
#define NEM_TRANSPARENT_FOG

#include "heightFog.hlsli"

cbuffer FogDrawConstants : register(b11, space7) {
	uint fogDrawEnabled;
	uint fogBlendMode;
	uint2 fogDrawPadding;
};

// ブレンドの中立色へ減衰する
float4 BlendTransparentFog(float4 color, float4 fog, uint blendMode) {

	if (blendMode == 3u) color.rgb = lerp(1.0f.xxx, color.rgb, fog.a);
	else {
		color.rgb *= fog.a;
		if (blendMode == 0u) color.rgb += fog.rgb;
		else if (blendMode == 5u) color.rgb += fog.rgb * saturate(color.a);
	}
	return color;
}

// 透明物自身の深度で評価する
float4 ApplyTransparentFog(float4 color, float4 screenPosition) {

	if (fogDrawEnabled == 0u || (fogCameraPosition.w == 0.0f && fogAerialIndex == 0xffffffffu)) return color;
	float2 uv = screenPosition.xy * fogViewportParameters.xy;
	float4 position = mul(float4(uv * float2(2, -2) + float2(-1, 1), screenPosition.z, 1),
		fogInverseViewProjection);
	float4 fog = EvaluateSceneFog(position.xyz / position.w, uv, false);
	return BlendTransparentFog(color, fog, fogBlendMode);
}

#endif
