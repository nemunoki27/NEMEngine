#ifndef NEM_PARTICLE_SURFACE
#define NEM_PARTICLE_SURFACE

#define NEM_LIGHT_COUNTS_REGISTER b2, space7
#define NEM_DIRECTIONAL_LIGHTS_REGISTER t0, space7
#define NEM_POINT_LIGHTS_REGISTER t1, space7
#define NEM_SPOT_LIGHTS_REGISTER t2, space7
#define NEM_RECT_LIGHTS_REGISTER t3, space7
#include "../../Mesh/Common/pbrShading.hlsli"
#include "../../Common/descriptorHeapCompatibility.hlsli"

cbuffer ParticleSurfaceConstants : register(b5) {

	float4x4 particleInverseViewProjection;
	float4x4 particleCameraView;
	float4 particleCameraPosition;
	float4 particleCameraForward;
	uint particleSurfaceFlags;
	uint particleDepthAvailable;
	uint particleNormalAvailable;
	uint particleCustomDataAvailable;
	float4 particleSurfaceParameters;
};

cbuffer DirectionalShadowConstants : register(b3, space7) {

	float4x4 shadowViewProjections[4];
	float4 shadowCascadeSplits;
	uint shadowAvailable;
	uint shadowLightIndex;
	float shadowDepthBias;
	float _particleShadowPad;
};

cbuffer ParticleEnvironmentConstants : register(b4, space7) {

	float4 particleEnvironmentColorIntensity;
	uint particleIrradianceIndex;
	uint particleSpecularIndex;
	uint particleBRDFIndex;
	uint particlePhysicalEnvironment;
	float particleAmbientIntensity;
	uint3 _particleEnvironmentPad;
};

Texture2D<float> gDirectionalShadowMap0 : register(t13, space7);
Texture2D<float> gDirectionalShadowMap1 : register(t14, space7);
Texture2D<float> gDirectionalShadowMap2 : register(t15, space7);
Texture2D<float> gDirectionalShadowMap3 : register(t16, space7);
Texture2D<float4> particleNormalTexture : register(t31, space7);

Texture2D<float> gParticleSceneDepth : register(t30, space7);

float ParticleShadowDepth(uint cascade, int2 pixel) {

	if (cascade == 0u) return gDirectionalShadowMap0.Load(int3(pixel, 0));
	if (cascade == 1u) return gDirectionalShadowMap1.Load(int3(pixel, 0));
	if (cascade == 2u) return gDirectionalShadowMap2.Load(int3(pixel, 0));
	return gDirectionalShadowMap3.Load(int3(pixel, 0));
}

float ParticleShadowVisibility(float3 world) {

	if (shadowAvailable == 0u || (particleSurfaceFlags & 2u) == 0u) return 1;
	float depth = mul(float4(world, 1), particleCameraView).z;
	uint cascade = depth <= shadowCascadeSplits.x ? 0u : depth <= shadowCascadeSplits.y ? 1u :
		depth <= shadowCascadeSplits.z ? 2u : 3u;
	float4 clip = mul(float4(world, 1), shadowViewProjections[cascade]);
	float3 projected = clip.xyz / clip.w;
	float2 uv = projected.xy * float2(0.5f, -0.5f) + 0.5f;
	if (any(uv < 0) || any(uv > 1) || projected.z < 0 || projected.z > 1) return 1;
	uint width, height;
	if (cascade == 0u) gDirectionalShadowMap0.GetDimensions(width, height);
	else if (cascade == 1u) gDirectionalShadowMap1.GetDimensions(width, height);
	else if (cascade == 2u) gDirectionalShadowMap2.GetDimensions(width, height);
	else gDirectionalShadowMap3.GetDimensions(width, height);
	float2 coordinate = uv * float2(width, height) - 0.5f;
	int2 base = int2(floor(coordinate));
	float2 weight = frac(coordinate);
	float visibility[4];
	[unroll] for (uint i = 0; i < 4; ++i) {
		int2 pixel = clamp(base + int2(i & 1u, i >> 1u), 0, int2(width, height) - 1);
		visibility[i] = step(projected.z - shadowDepthBias, ParticleShadowDepth(cascade, pixel));
	}
	return lerp(lerp(visibility[0], visibility[1], weight.x), lerp(visibility[2], visibility[3], weight.x), weight.y);
}

float ParticleSoftFade(float4 position, float3 world) {

	if ((particleSurfaceFlags & 8u) == 0u || particleDepthAvailable == 0u) return 1;
	uint width, height;
	gParticleSceneDepth.GetDimensions(width, height);
	int2 pixel = clamp(int2(position.xy), 0, int2(width, height) - 1);
	float depth = gParticleSceneDepth.Load(int3(pixel, 0));
	if (depth >= 1.0f) return 1;
	float2 uv = (float2(pixel) + 0.5f) / float2(width, height);
	float4 scene = mul(float4(uv * float2(2, -2) + float2(-1, 1), depth, 1), particleInverseViewProjection);
	float separation = dot(scene.xyz / scene.w - world, particleCameraForward.xyz);
	return saturate(separation / max(particleSurfaceParameters.x, 0.0001f));
}

float3x3 ParticleSurfaceFrame(float2 uv, float3 normal, float4 tangent, float3 world) {

	float3 N = normalize(normal);
	float3 T = tangent.xyz - N * dot(N, tangent.xyz);
	if (dot(T, T) < 0.00001f) T = cross(abs(N.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0), N);
	T = normalize(T);
	float sign = tangent.w;
	float3 dx = ddx(world), dy = ddy(world);
	float2 uvX = ddx(uv), uvY = ddy(uv);
	float determinant = uvX.x * uvY.y - uvX.y * uvY.x;
	if (abs(determinant) > 0.00000001f) {
		// 変形後の位置とUVから法線Mapの基底を揃える
		float3 direction = (dx * uvY.y - dy * uvX.y) / determinant;
		direction -= N * dot(N, direction);
		if (dot(direction, direction) > 0.00001f) T = normalize(direction);
		float3 bitangent = (dy * uvX.x - dx * uvY.x) / determinant;
		sign = dot(cross(N, T), bitangent) < 0 ? -1 : 1;
	}
	return float3x3(T, cross(N, T) * sign, N);
}

float3 ParticleSurfaceNormal(float2 uv, float3 normal, float4 tangent, float3 world) {

	float3 N = normalize(normal);
	if (particleNormalAvailable == 0u) return N;
	float3 sampled = particleNormalTexture.Sample(gSampler, uv).xyz * 2 - 1;
	sampled.xy *= particleSurfaceParameters.y;
	return normalize(mul(normalize(sampled), ParticleSurfaceFrame(uv, N, tangent, world)));
}

float3 ParticleEnvironmentLighting(float3 albedo, float3 N, float3 V, float metallic, float roughness, float3 F0) {

	float NdotV = saturate(dot(N, V));
	float3 fresnel = FresnelSchlick(NdotV, F0);
	float3 color = particleAmbientIntensity * albedo;
	if (particleIrradianceIndex != 0xFFFFFFFFu) {
		TextureCube<float4> irradiance = NEM_TEXTURECUBE(particleIrradianceIndex);
		float3 weight = particlePhysicalEnvironment != 0u ? (1 - fresnel) * (1 - metallic) : 1.0f.xxx;
		color = irradiance.SampleLevel(gSampler, N, 0).rgb * particleEnvironmentColorIntensity.rgb *
			particleEnvironmentColorIntensity.w * albedo * weight;
	}
	if (particleSpecularIndex != 0xFFFFFFFFu) {
		TextureCube<float4> environment = NEM_TEXTURECUBE(particleSpecularIndex);
		uint width, height, levels;
		environment.GetDimensions(0u, width, height, levels);
		float mip = roughness * max(float(levels) - 1, 0);
		float3 reflection = environment.SampleLevel(gSampler, reflect(-V, N), mip).rgb *
			particleEnvironmentColorIntensity.rgb * particleEnvironmentColorIntensity.w;
		float3 weight = fresnel * (1 - roughness * 0.75f);
		if (particlePhysicalEnvironment != 0u && particleBRDFIndex != 0xFFFFFFFFu) {
			Texture2D<float4> brdf = NEM_TEXTURE2D(particleBRDFIndex);
			float2 integral = brdf.SampleLevel(gSampler, float2(NdotV, roughness), 0).rg;
			weight = F0 * integral.x + integral.y;
		}
		color += reflection * weight;
	}
	return color;
}

float3 ParticleSurfaceLighting(float3 albedo, float3 world, float3 N, float metallic, float roughness) {

	if ((particleSurfaceFlags & 1u) == 0u) return albedo;
	float3 V = normalize(particleCameraPosition.xyz - world);
	float3 F0 = lerp(0.04f.xxx, albedo, metallic);
	float3 lighting = 0;
	[loop] for (uint i = 0; i < directionalCount; ++i) {
		float visibility = i == shadowLightIndex ? ParticleShadowVisibility(world) : 1;
		visibility = lerp(1, visibility, gDirectionalLights[i].shadowStrength);
		lighting += EvaluatePBRDirectionalLight(gDirectionalLights[i], N, V, albedo, metallic, roughness, F0) * visibility;
	}
	[loop] for (uint pi = 0; pi < pointCount; ++pi)
		lighting += EvaluatePBRPointLight(gPointLights[pi], world, N, V, albedo, metallic, roughness, F0);
	[loop] for (uint si = 0; si < spotCount; ++si)
		lighting += EvaluatePBRSpotLight(gSpotLights[si], world, N, V, albedo, metallic, roughness, F0);
	[loop] for (uint ri = 0; ri < rectCount; ++ri)
		lighting += EvaluatePBRRectLight(gRectLights[ri], world, N, V, albedo, metallic, roughness, F0);
	if ((particleSurfaceFlags & 4u) != 0u) lighting += ParticleEnvironmentLighting(albedo, N, V, metallic, roughness, F0);
	return lighting;
}

#endif
