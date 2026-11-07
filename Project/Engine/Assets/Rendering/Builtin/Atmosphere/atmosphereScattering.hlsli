#ifndef NEM_ATMOSPHERE_SCATTERING
#define NEM_ATMOSPHERE_SCATTERING
#include "../Common/descriptorHeapCompatibility.hlsli"

// Hillaire, A Scalable and Production Ready Sky and Atmosphere Rendering Technique, EGSR 2020
struct AtmosphereParameters {
	float4 rayleigh;
	float4 mie;
	float4 ozone;
	float4 ground;
	float4 planet;
	float4 sun;
	float4 solar;
	float4 moon;
	float4 lunar;
	float4 moonDisk;
};

float AtmosphereBoundary(float3 p, float3 direction, float radius) {
	float b = dot(p, direction);
	float c = dot(p, p) - radius * radius;
	float discriminant = b * b - c;
	if (discriminant < 0.0f) return -1.0f;
	float root = sqrt(discriminant);
	float nearDistance = -b - root;
	return nearDistance >= 0.0f ? nearDistance : -b + root;
}

void AtmosphereMedium(AtmosphereParameters a, float3 position, out float3 scattering,
	out float3 extinction, out float rayleighDensity, out float mieDensity) {
	float altitude = max(length(position) - a.planet.x, 0.0f);
	rayleighDensity = exp(-altitude / max(a.rayleigh.w, 0.001f));
	mieDensity = exp(-altitude / max(a.mie.z, 0.001f));
	float ozoneDensity = saturate(1.0f - abs(altitude - a.ozone.w) / max(a.ground.w * 0.5f, 0.001f));
	scattering = a.rayleigh.rgb * rayleighDensity + a.mie.x * mieDensity;
	extinction = scattering + a.mie.y * mieDensity + a.ozone.rgb * ozoneDensity;
}

float2 AtmosphereTransmittanceUV(AtmosphereParameters a, float3 position, float3 direction) {
	float bottom = a.planet.x;
	float top = bottom + a.planet.y;
	float radius = clamp(length(position), bottom, top);
	float cosine = dot(normalize(position), direction);
	float horizon = sqrt(max(top * top - bottom * bottom, 0.0f));
	float rho = sqrt(max(radius * radius - bottom * bottom, 0.0f));
	float distance = max(-radius * cosine + sqrt(max(radius * radius * (cosine * cosine - 1.0f) + top * top, 0.0f)), 0.0f);
	float minimum = top - radius;
	float2 uv = float2((distance - minimum) / max(rho + horizon - minimum, 0.001f), rho / max(horizon, 0.001f));
	return (saturate(uv) * float2(255, 63) + 0.5f) / float2(256, 64);
}

float3 AtmosphereSunTransmittance(AtmosphereParameters a, float3 position, float3 direction,
	uint transmittanceIndex, SamplerState linearSampler) {
	if (AtmosphereBoundary(position, direction, a.planet.x) > 0.0f) return 0.0f;
	Texture2D<float4> lookup = NEM_TEXTURE2D(transmittanceIndex);
	return lookup.SampleLevel(linearSampler, AtmosphereTransmittanceUV(a, position, direction), 0).rgb;
}

float3 AtmosphereMultiScattering(AtmosphereParameters a, float3 position, uint multiIndex,
	SamplerState linearSampler, float3 lightDirection) {
	Texture2D<float4> lookup = NEM_TEXTURE2D(multiIndex);
	float2 uv = float2(dot(normalize(position), lightDirection) * 0.5f + 0.5f,
		saturate((length(position) - a.planet.x) / a.planet.y));
	return lookup.SampleLevel(linearSampler, uv, 0).rgb;
}

float2 AtmospherePhase(float cosine, float g) {
	float rayleigh = 3.0f * (1.0f + cosine * cosine) / (16.0f * 3.14159265f);
	float mie = 3.0f * (1.0f - g * g) * (1.0f + cosine * cosine) /
		(8.0f * 3.14159265f * (2.0f + g * g) * pow(max(1.0f + g * g - 2.0f * g * cosine, 0.001f), 1.5f));
	return float2(rayleigh, mie);
}

float3 AtmosphereIncoming(AtmosphereParameters a, float3 position, float3 lightDirection, float2 phase,
	float3 scattering, float rayleighDensity, float mieDensity, uint transmittanceIndex, uint multiIndex, SamplerState linearSampler) {
	float3 direct = AtmosphereSunTransmittance(a, position, lightDirection, transmittanceIndex, linearSampler) *
		(a.rayleigh.rgb * rayleighDensity * phase.x + a.mie.x * mieDensity * phase.y);
	return direct + scattering * AtmosphereMultiScattering(a, position, multiIndex, linearSampler, lightDirection);
}

float4 IntegrateAtmosphere(AtmosphereParameters a, float3 position, float3 direction, float distanceKm,
	uint transmittanceIndex, uint multiIndex, SamplerState linearSampler, uint sampleCount, bool includeGround) {
	float topDistance = AtmosphereBoundary(position, direction, a.planet.x + a.planet.y);
	if (topDistance <= 0.0f) return float4(0, 0, 0, 1);
	float groundDistance = AtmosphereBoundary(position, direction, a.planet.x);
	float rayLength = min(distanceKm, topDistance);
	if (groundDistance > 0.0f) rayLength = min(rayLength, groundDistance);
	float g = clamp(a.mie.w, -0.95f, 0.95f);
	float2 sunPhase = AtmospherePhase(dot(direction, a.sun.xyz), g);
	float2 moonPhase = AtmospherePhase(dot(direction, a.moon.xyz), g);
	float3 luminance = 0.0f;
	float3 throughput = 1.0f;
	float stepLength = rayLength / float(sampleCount);
	for (uint i = 0; i < sampleCount; ++i) {
		float3 samplePosition = position + direction * ((float(i) + 0.5f) * stepLength);
		float3 scattering, extinction;
		float rayleighDensity, mieDensity;
		AtmosphereMedium(a, samplePosition, scattering, extinction, rayleighDensity, mieDensity);
		float3 incoming = AtmosphereIncoming(a, samplePosition, a.sun.xyz, sunPhase, scattering,
			rayleighDensity, mieDensity, transmittanceIndex, multiIndex, linearSampler) * a.solar.rgb;
		if (any(a.lunar.rgb > 0.0f)) incoming += AtmosphereIncoming(a, samplePosition, a.moon.xyz, moonPhase, scattering,
			rayleighDensity, mieDensity, transmittanceIndex, multiIndex, linearSampler) * a.lunar.rgb;
		float3 segmentTransmittance = exp(-extinction * stepLength);
		luminance += throughput * incoming * (1.0f - segmentTransmittance) / max(extinction, 1e-6f);
		throughput *= segmentTransmittance;
	}
	if (includeGround && groundDistance > 0.0f && groundDistance <= min(distanceKm, topDistance)) {
		float3 groundPosition = position + direction * groundDistance;
		float3 groundNormal = normalize(groundPosition);
		groundPosition += groundNormal * 0.002f;
		float3 direct = AtmosphereSunTransmittance(a, groundPosition, a.sun.xyz, transmittanceIndex, linearSampler) *
			saturate(dot(groundNormal, a.sun.xyz)) / 3.14159265f;
		float3 indirect = AtmosphereMultiScattering(a, groundPosition, multiIndex, linearSampler, a.sun.xyz);
		float3 groundLight = (direct + indirect) * a.solar.rgb;
		if (any(a.lunar.rgb > 0.0f)) {
			float3 moonDirect = AtmosphereSunTransmittance(a, groundPosition, a.moon.xyz, transmittanceIndex, linearSampler) *
				saturate(dot(groundNormal, a.moon.xyz)) / 3.14159265f;
			groundLight += (moonDirect + AtmosphereMultiScattering(a, groundPosition, multiIndex, linearSampler, a.moon.xyz)) * a.lunar.rgb;
		}
		luminance += throughput * a.ground.rgb * groundLight;
	}
	return float4(clamp(luminance, 0.0f, 65000.0f), dot(throughput, float3(0.33333333f, 0.33333333f, 0.33333333f)));
}
#endif
