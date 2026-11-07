#include "atmosphereScattering.hlsli"
#include "../Lighting/environmentSampling.hlsli"

cbuffer AtmosphereConstants : register(b0) {
	AtmosphereParameters atmosphere;
	uint4 lookupIndices;
	float4 cameraPositionDistance;
	float4x4 inverseViewProjection;
};
RWTexture2D<float4> gLookupOutput : register(u0);
RWTexture2DArray<float4> gCubeOutput : register(u1);
SamplerState gLinearSampler : register(s0);

[numthreads(8, 8, 1)]
void TransmittanceMain(uint3 pixel : SV_DispatchThreadID) {
	if (any(pixel.xy >= uint2(256, 64))) return;
	float2 uv = float2(pixel.xy) / float2(255, 63);
	float bottom = atmosphere.planet.x;
	float top = bottom + atmosphere.planet.y;
	float horizon = sqrt(top * top - bottom * bottom);
	float rho = horizon * uv.y;
	float radius = sqrt(rho * rho + bottom * bottom);
	float minimum = top - radius;
	float distance = minimum + uv.x * (rho + horizon - minimum);
	float cosine = distance > 0.0001f ? clamp((horizon * horizon - rho * rho - distance * distance) /
		(2.0f * radius * distance), -1.0f, 1.0f) : 1.0f;
	float3 position = float3(0, radius, 0);
	float3 direction = float3(sqrt(saturate(1.0f - cosine * cosine)), cosine, 0);
	float stepLength = max(distance, 0.0f) / 40.0f;
	float3 opticalDepth = 0.0f;
	for (uint i = 0; i < 40u; ++i) {
		float3 scattering, extinction;
		float rayleighDensity, mieDensity;
		AtmosphereMedium(atmosphere, position + direction * ((float(i) + 0.5f) * stepLength),
			scattering, extinction, rayleighDensity, mieDensity);
		opticalDepth += extinction * stepLength;
	}
	gLookupOutput[pixel.xy] = float4(exp(-opticalDepth), 1);
}

[numthreads(8, 8, 1)]
void MultiScatteringMain(uint3 pixel : SV_DispatchThreadID) {
	if (any(pixel.xy >= 32u)) return;
	float2 uv = (float2(pixel.xy) + 0.5f) / 32.0f;
	float cosine = uv.x * 2.0f - 1.0f;
	float3 sunDirection = float3(sqrt(saturate(1.0f - cosine * cosine)), cosine, 0);
	float3 position = float3(0, atmosphere.planet.x + uv.y * atmosphere.planet.y, 0);
	float3 secondOrder = 0;
	float3 transfer = 0;
	for (uint directionIndex = 0; directionIndex < 64u; ++directionIndex) {
		float y = 1.0f - 2.0f * (float(directionIndex) + 0.5f) / 64.0f;
		float angle = float(directionIndex) * 2.39996323f;
		float radius = sqrt(saturate(1.0f - y * y));
		float3 direction = float3(radius * cos(angle), y, radius * sin(angle));
		float distance = AtmosphereBoundary(position, direction, atmosphere.planet.x + atmosphere.planet.y);
		float groundDistance = AtmosphereBoundary(position, direction, atmosphere.planet.x);
		if (groundDistance > 0.0f) distance = min(distance, groundDistance);
		float stepLength = max(distance, 0.0f) / 20.0f;
		float3 throughput = 1;
		for (uint i = 0; i < 20u; ++i) {
			float3 samplePosition = position + direction * ((float(i) + 0.5f) * stepLength);
			float3 scattering, extinction;
			float rayleighDensity, mieDensity;
			AtmosphereMedium(atmosphere, samplePosition, scattering, extinction, rayleighDensity, mieDensity);
			float3 segment = exp(-extinction * stepLength);
			float3 weight = throughput * scattering * (1.0f - segment) / max(extinction, 1e-6f);
			transfer += weight;
			secondOrder += weight * AtmosphereSunTransmittance(atmosphere, samplePosition, sunDirection,
				lookupIndices.x, gLinearSampler) / (4.0f * 3.14159265f);
			throughput *= segment;
		}
		if (groundDistance > 0.0f) {
			float3 samplePosition = position + direction * groundDistance;
			secondOrder += throughput * atmosphere.ground.rgb * saturate(dot(normalize(samplePosition), sunDirection)) *
				AtmosphereSunTransmittance(atmosphere, samplePosition + normalize(samplePosition) * 0.001f, sunDirection,
					lookupIndices.x, gLinearSampler) / 3.14159265f;
		}
	}
	gLookupOutput[pixel.xy] = float4(secondOrder / 64.0f / max(1.0f - transfer / 64.0f, 0.001f), 1);
}

float3 SkyDirection(float2 uv) {
	float latitude = (uv.y * 2.0f - 1.0f);
	latitude = sign(latitude) * latitude * latitude * 1.570796327f;
	float longitude = uv.x * 6.283185307f;
	return float3(cos(latitude) * cos(longitude), sin(latitude), cos(latitude) * sin(longitude));
}

[numthreads(8, 8, 1)]
void SkyViewMain(uint3 pixel : SV_DispatchThreadID) {
	if (any(pixel.xy >= uint2(200, 100))) return;
	float3 direction = SkyDirection((float2(pixel.xy) + 0.5f) / float2(200, 100));
	float3 position = float3(0, atmosphere.planet.x + max(atmosphere.planet.w, 0.002f), 0);
	gLookupOutput[pixel.xy] = IntegrateAtmosphere(atmosphere, position, direction, 100000.0f,
		lookupIndices.x, lookupIndices.y, gLinearSampler, 30u, true);
}

[numthreads(8, 8, 1)]
void CubemapMain(uint3 pixel : SV_DispatchThreadID) {
	if (any(pixel.xy >= 128u) || pixel.z >= 6u) return;
	float3 direction = EnvironmentCubeDirection(pixel.z, (float2(pixel.xy) + 0.5f) / 128.0f);
	float latitude = asin(clamp(direction.y, -1.0f, 1.0f)) / 1.570796327f;
	float2 uv = float2(frac(atan2(direction.z, direction.x) / 6.283185307f + 1.0f),
		0.5f + 0.5f * sign(latitude) * sqrt(abs(latitude)));
	Texture2D<float4> sky = NEM_TEXTURE2D(lookupIndices.z);
	float3 color = sky.SampleLevel(gLinearSampler, uv, 0).rgb;
	// 太陽円盤は低解像度の空LUTに含めず、Cubemapの画素被覆で積分する
	float pixelRadius = 1.0f / 128.0f;
	float radius = max(atmosphere.sun.w, 0.0001f);
	float angle = acos(clamp(dot(direction, atmosphere.sun.xyz), -1.0f, 1.0f));
	float coverage = 1.0f - smoothstep(max(radius - pixelRadius, 0.0f), radius + pixelRadius, angle);
	float3 transmittance = AtmosphereSunTransmittance(atmosphere,
		float3(0, atmosphere.planet.x + 0.002f, 0), atmosphere.sun.xyz, lookupIndices.x, gLinearSampler);
	color += float(lookupIndices.w) * transmittance * atmosphere.solar.rgb * coverage / (3.14159265f * max(radius * radius, pixelRadius * pixelRadius));
	float moonRadius = max(atmosphere.moon.w, 0.0001f);
	float moonAngle = acos(clamp(dot(direction, atmosphere.moon.xyz), -1.0f, 1.0f));
	float moonCoverage = 1.0f - smoothstep(max(moonRadius - pixelRadius, 0.0f), moonRadius + pixelRadius, moonAngle);
	float3 moonTransmittance = AtmosphereSunTransmittance(atmosphere,
		float3(0, atmosphere.planet.x + 0.002f, 0), atmosphere.moon.xyz, lookupIndices.x, gLinearSampler);
	color += float(lookupIndices.w) * atmosphere.lunar.w * atmosphere.moonDisk.rgb * moonTransmittance *
		atmosphere.lunar.rgb * moonCoverage / (3.14159265f * max(moonRadius * moonRadius, pixelRadius * pixelRadius));
	gCubeOutput[pixel] = float4(min(color, 65000.0f), 1);
}

[numthreads(8, 8, 1)]
void AerialMain(uint3 pixel : SV_DispatchThreadID) {
	if (any(pixel.xy >= uint2(32, 1024))) return;
	uint slice = pixel.y / 32u;
	float2 uv = (float2(pixel.x, pixel.y % 32u) + 0.5f) / 32.0f;
	float4 farPosition = mul(float4(uv * float2(2, -2) + float2(-1, 1), 1, 1), inverseViewProjection);
	farPosition.xyz /= farPosition.w;
	float3 direction = normalize(farPosition.xyz - cameraPositionDistance.xyz);
	float3 position = float3(0, atmosphere.planet.x + max((cameraPositionDistance.y - atmosphere.planet.z) * 0.001f, 0.002f), 0);
	float depth = (float(slice) + 0.5f) / 32.0f;
	float distance = depth * depth * cameraPositionDistance.w * 0.001f;
	gLookupOutput[pixel.xy] = IntegrateAtmosphere(atmosphere, position, direction, distance,
		lookupIndices.x, lookupIndices.y, gLinearSampler, 30u, false);
}
