#ifndef NEM_HEIGHT_FOG
#define NEM_HEIGHT_FOG
#include "atmosphereAerial.hlsli"

SamplerState fogLinearSampler : register(s10, space7);

cbuffer HeightFogConstants : register(b10, space7) {

	float4x4 fogInverseViewProjection;
	float4 fogCameraPosition;
	float4 fogColorDensity;
	float4 fogHeightParameters;
	float4 fogDistanceParameters;
	float4 fogDirectionalColor;
	float4 fogSunDirection;
	float4 fogSecondaryParameters;
	float4 fogViewportParameters;
	uint fogAerialIndex;
	uint fogIrradianceIndex;
	float fogAerialMaxDistance;
	float fogAerialStrength;
	float4 fogSkyContribution;
};

// 高さ方向の指数密度を光線区間で積分する
float IntegrateHeightFog(float density, float falloff, float baseHeight,
	float startHeight, float verticalDistance, float distance) {

	if (density <= 0.0f || distance <= 0.0f) return 0.0f;
	float exponent = falloff * verticalDistance;
	float magnitude = abs(exponent);
	float peak = -falloff * (startHeight - baseHeight) + max(-exponent, 0.0f);
	float integral = magnitude < 0.001f ?
		1.0f - magnitude * 0.5f + magnitude * magnitude / 6.0f : (1.0f - exp(-magnitude)) / magnitude;
	// 密度の高い端点を基準にし、指数同士の乗算とoverflowを避ける
	if (peak > 50.0f) {
		float logDepth = log(density) + log(distance) + peak + log(integral);
		return exp(min(logDepth, 4.38202663f));
	}
	return min(80.0f, density * exp(peak) * integral * distance);
}

// RGBは散乱色、Aは透過率を返す
float4 EvaluateHeightFog(float3 worldPosition) {

	float3 ray = worldPosition - fogCameraPosition.xyz;
	float distance = length(ray);
	if (fogCameraPosition.w == 0.0f || distance <= fogDistanceParameters.x ||
		(fogDistanceParameters.z > 0.0f && distance > fogDistanceParameters.z)) return float4(0, 0, 0, 1);
	float3 direction = ray / max(distance, 0.0001f);
	float start = fogDistanceParameters.x;
	float lengthInFog = distance - start;
	float startHeight = fogCameraPosition.y + direction.y * start;
	float opticalDepth = IntegrateHeightFog(fogColorDensity.w, fogHeightParameters.x, fogHeightParameters.y,
		startHeight, direction.y * lengthInFog, lengthInFog);
	opticalDepth += IntegrateHeightFog(fogSecondaryParameters.x, fogSecondaryParameters.y,
		fogHeightParameters.y + fogSecondaryParameters.z, startHeight, direction.y * lengthInFog, lengthInFog);
	float opacity = min(1.0f - exp(-min(opticalDepth, 80.0f)), fogDistanceParameters.y);
	float3 color = fogColorDensity.rgb;
	if (fogIrradianceIndex != 0xffffffffu && fogSkyContribution.w > 0.0f) {
		TextureCube<float4> environment = NEM_TEXTURECUBE(fogIrradianceIndex);
		color += environment.SampleLevel(fogLinearSampler, float3(0, 1, 0), 0).rgb * fogSkyContribution.rgb * fogSkyContribution.w;
	}
	if (fogHeightParameters.w > 0.0f && distance > fogDirectionalColor.w) {
		float directional = pow(saturate(dot(direction, -fogSunDirection.xyz)), fogHeightParameters.z);
		color += fogDirectionalColor.rgb * directional * fogHeightParameters.w;
	}
	return float4(color * opacity, 1.0f - opacity);
}

float4 EvaluateSceneFog(float3 worldPosition, float2 screenUV, bool background = false) {

	float4 aerial = float4(0, 0, 0, 1);
	if (!background && fogAerialIndex != 0xffffffffu) {
		aerial = SampleAtmosphereAerial(fogAerialIndex, fogLinearSampler, screenUV,
			length(worldPosition - fogCameraPosition.xyz), fogAerialMaxDistance);
		float transmission = pow(max(aerial.a, 0.000001f), max(fogAerialStrength, 0.0f));
		aerial.rgb *= aerial.a < 0.99999f ? (1.0f - transmission) / (1.0f - aerial.a) : max(fogAerialStrength, 0.0f);
		aerial.a = transmission;
	}
	float4 fog = EvaluateHeightFog(worldPosition);
	return float4(aerial.rgb * fog.a + fog.rgb, aerial.a * fog.a);
}

#endif
