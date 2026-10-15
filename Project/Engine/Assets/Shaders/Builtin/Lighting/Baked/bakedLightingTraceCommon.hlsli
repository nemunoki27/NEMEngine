#include "bakedLightingTypes.hlsli"
#include "../../Common/descriptorHeapCompatibility.hlsli"
#include "../../Common/pbrMath.hlsli"
#include "../../Common/normalEncoding.hlsli"
#include "../environmentSampling.hlsli"
#define NEM_LIGHT_COUNTS_REGISTER b2
#include "../lightShaderData.hlsli"
#include "../lightAttenuation.hlsli"

cbuffer BakedLightingBakeConstants : register(b0) {

	uint stage;
	uint firstTask;
	uint taskCount;
	uint sampleStart;
	uint samplesPerTask;
	uint bounceCount;
	uint skyboxIndex;
	float normalBias;
	float4 skyColor;
	uint lightmapCount;
	uint captureSize;
	uint captureCount;
	float skyIntensity;
	float maximumDistance;
	uint3 _bakePad;
};
struct BakedMaterial {

	float4 baseColor;
	float4 emissive;
	uint baseColorTexture;
	uint emissiveTexture;
	uint opacityTexture;
	uint masked;
	float metallic;
	float roughness;
	float alphaCutoff;
	uint metallicTexture;
	uint metallicRoughnessTexture;
	uint roughnessTexture;
	uint normalTexture;
	float opacity;
};
StructuredBuffer<BakedTriangle> gBakeTriangles : register(t0);
StructuredBuffer<BakedTexel> gBakeTexels : register(t1);
StructuredBuffer<BakedMaterial> gBakeMaterials : register(t2);
RaytracingAccelerationStructure gBakeTLAS : register(t3);
StructuredBuffer<BakedProbe> gBakeProbes : register(t4);
StructuredBuffer<BakedCapture> gBakeCaptures : register(t5);
RWStructuredBuffer<float4> gBakeOutput : register(u0);
RWStructuredBuffer<BakedProbe> gBakeProbeOutput : register(u1);
SamplerState gBakeSampler : register(s0);

// Frame番号に依存しない乱数系列を使う
uint BakeHash(uint value) {

	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ (value >> 16);
}

float BakeRandom(inout uint seed) {

	seed = BakeHash(seed + 0x9e3779b9u);
	return float(seed >> 8) * (1.0f / 16777216.0f);
}

float2 BakeRandom2(inout uint seed) {

	float first = BakeRandom(seed);
	return float2(first, BakeRandom(seed));
}

float3 BakeHemisphere(float3 normal, inout uint seed) {

	float2 sampleUV = BakeRandom2(seed);
	float radius = sqrt(sampleUV.x);
	float phi = 2.0f * kEnvironmentPI * sampleUV.y;
	float3 up = abs(normal.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
	float3 tangent = normalize(cross(up, normal));
	return normalize(tangent * radius * cos(phi) + cross(normal, tangent) * radius * sin(phi) + normal * sqrt(1.0f - sampleUV.x));
}

float3 BakeSphere(inout uint seed) {

	float2 sampleUV = BakeRandom2(seed);
	float z = sampleUV.x * 2.0f - 1.0f;
	float radius = sqrt(saturate(1.0f - z * z));
	return float3(radius * cos(sampleUV.y * 2.0f * kEnvironmentPI), radius * sin(sampleUV.y * 2.0f * kEnvironmentPI), z);
}

float2 BakeTriangleUV(BakedTriangle geometry, float2 barycentric) {

	return geometry.vertices[0].uv * (1.0f - barycentric.x - barycentric.y) +
		geometry.vertices[1].uv * barycentric.x + geometry.vertices[2].uv * barycentric.y;
}

float4 BakeTexture(uint index, float2 uv) {

	if (index == 0xFFFFFFFFu) return 1.0f.xxxx;
	Texture2D<float4> texture = NEM_TEXTURE2D(index);
	return texture.SampleLevel(gBakeSampler, uv, 0.0f);
}

// 切り抜き面を候補交差で評価する
bool BakeTrace(float3 origin, float3 direction, float distance, out uint triangleIndex, out float2 barycentric, out float hitDistance) {

	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction;
	ray.TMin = max(normalBias * 0.1f, 0.00001f);
	ray.TMax = max(distance, ray.TMin * 2.0f);
	RayQuery<RAY_FLAG_NONE> query;
	query.TraceRayInline(gBakeTLAS, RAY_FLAG_NONE, 0xFF, ray);
	while (query.Proceed()) {
		if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
		BakedTriangle geometry = gBakeTriangles[query.CandidatePrimitiveIndex()];
		BakedMaterial material = gBakeMaterials[geometry.materialIndex];
		if (material.masked != 0u) {
			float2 uv = BakeTriangleUV(geometry, query.CandidateTriangleBarycentrics());
			float alpha = material.baseColor.a * material.opacity * BakeTexture(material.baseColorTexture, uv).a;
			alpha *= BakeTexture(material.opacityTexture, uv).r;
			if (alpha < material.alphaCutoff) continue;
		}
		query.CommitNonOpaqueTriangleHit();
	}
	triangleIndex = 0u;
	barycentric = 0.0f;
	hitDistance = distance;
	if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
	triangleIndex = query.CommittedPrimitiveIndex();
	barycentric = query.CommittedTriangleBarycentrics();
	hitDistance = query.CommittedRayT();
	return true;
}

float BakeShadow(float3 origin, float3 direction, float distance, float strength) {

	uint geometry;
	float2 barycentric;
	float hitDistance;
	return BakeTrace(origin, direction, distance, geometry, barycentric, hitDistance) ? 1.0f - saturate(strength) : 1.0f;
}

// Captureの初回だけ鏡面を含む表面反射を評価する
float3 BakeLightResponse(float3 normal, float3 view, float3 light, float3 albedo, float metallic, float roughness, bool capture) {

	if (!capture) return 1.0f.xxx;
	float3 halfDirection = normalize(view + light);
	float nDotV = max(saturate(dot(normal, view)), 1e-4f);
	float nDotL = max(saturate(dot(normal, light)), 1e-4f);
	float3 fresnel = FresnelSchlick(saturate(dot(view, halfDirection)), lerp(0.04f.xxx, albedo, metallic));
	float distribution = EvalD(saturate(dot(normal, halfDirection)), max(roughness, 0.04f));
	float geometry = EvalG(nDotV, nDotL, max(roughness, 0.04f));
	return (1.0f - fresnel) * (1.0f - metallic) * albedo +
		distribution * geometry * fresnel * kEnvironmentPI / max(4.0f * nDotV * nDotL, 1e-4f);
}

// 各固定ライトの入射光を遮蔽付きで評価する
float3 BakeDirect(float3 position, float3 normal, inout uint seed, float3 view, float3 albedo, float metallic, float roughness, bool capture) {

	float3 irradiance = 0.0f;
	float3 origin = position + normal * normalBias;
	for (uint index = 0; index < directionalCount; ++index) {
		DirectionalLight light = gDirectionalLights[index];
		float3 direction = -normalize(light.direction);
		float3 tangent = normalize(cross(abs(direction.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0), direction));
		float2 randomUV = BakeRandom2(seed);
		float radius = sqrt(randomUV.x) * tan(radians(light.shadowAngularRadius));
		direction = normalize(direction + radius * (cos(randomUV.y * 6.2831853f) * tangent +
			sin(randomUV.y * 6.2831853f) * cross(direction, tangent)));
		float cosine = saturate(dot(normal, direction));
		if (cosine > 0.0f) irradiance += light.color.rgb * light.intensity * cosine * BakeShadow(origin, direction, maximumDistance, light.shadowStrength) *
			BakeLightResponse(normal, view, direction, albedo, metallic, roughness, capture);
	}
	for (uint index = 0; index < pointCount; ++index) {
		PointLight light = gPointLights[index];
		float3 delta = light.pos + BakeSphere(seed) * light.shadowRadius - position;
		float distance = length(delta);
		float3 direction = delta / max(distance, 1e-6f);
		float cosine = saturate(dot(normal, direction));
		if (cosine > 0.0f && distance < light.radius) irradiance += light.color.rgb * light.intensity * cosine *
			ComputeDistanceAttenuation(distance, light.radius, light.decay) * BakeShadow(origin, direction, max(distance - normalBias, normalBias), light.shadowStrength) *
			BakeLightResponse(normal, view, direction, albedo, metallic, roughness, capture);
	}
	for (uint index = 0; index < spotCount; ++index) {
		SpotLight light = gSpotLights[index];
		float3 delta = light.pos + BakeSphere(seed) * light.shadowRadius - position;
		float distance = length(delta);
		float3 direction = delta / max(distance, 1e-6f);
		float cone = saturate((dot(-direction, normalize(light.direction)) - light.cosAngle) / max(light.cosFalloffStart - light.cosAngle, 1e-5f));
		cone *= cone;
		float cosine = saturate(dot(normal, direction));
		if (cosine * cone > 0.0f && distance < light.distance) irradiance += light.color.rgb * light.intensity * cosine * cone *
			ComputeDistanceAttenuation(distance, light.distance, light.decay) * BakeShadow(origin, direction, max(distance - normalBias, normalBias), light.shadowStrength) *
			BakeLightResponse(normal, view, direction, albedo, metallic, roughness, capture);
	}
	for (uint index = 0; index < rectCount; ++index) {
		RectLight light = gRectLights[index];
		float2 randomUV = BakeRandom2(seed) - 0.5f;
		float3 emitter = light.pos + randomUV.x * light.right * light.sourceWidth + randomUV.y * light.up * light.sourceHeight;
		float3 delta = emitter - position;
		float distance = length(delta);
		float3 direction = delta / max(distance, 1e-6f);
		float cosine = saturate(dot(normal, direction)) * saturate(dot(light.direction, -direction)) * ComputeRectBarnAttenuation(light, position);
		if (cosine > 0.0f && distance < light.attenuationRadius) irradiance += light.color.rgb * light.intensity * cosine *
			ComputeDistanceAttenuation(length(light.pos - position), light.attenuationRadius, light.decay) *
			BakeShadow(origin, direction, max(distance - normalBias, normalBias), light.shadowStrength) *
			BakeLightResponse(normal, view, direction, albedo, metallic, roughness, capture);
	}
	return irradiance / kEnvironmentPI;
}

// 二次面の拡散反射を追跡し受光面のAlbedoを含めず返す
float3 BakeRadiance(float3 origin, float3 direction, inout uint seed, out float firstDistance, out float backface) {

	float3 result = 0.0f;
	float3 throughput = 1.0f;
	firstDistance = maximumDistance;
	backface = 0.0f;
	for (uint bounce = 0; bounce < bounceCount; ++bounce) {
		uint triangleIndex;
		float2 barycentric;
		float hitDistance;
		if (!BakeTrace(origin, direction, maximumDistance, triangleIndex, barycentric, hitDistance)) {
			if (skyboxIndex != 0xFFFFFFFFu) {
				TextureCube<float4> sky = NEM_TEXTURECUBE(skyboxIndex);
				result += throughput * sky.SampleLevel(gBakeSampler, direction, 0.0f).rgb * skyColor.rgb * skyIntensity;
			}
			break;
		}
		BakedTriangle geometry = gBakeTriangles[triangleIndex];
		BakedMaterial material = gBakeMaterials[geometry.materialIndex];
		float3 weights = float3(1.0f - barycentric.x - barycentric.y, barycentric);
		float3 position = origin + direction * hitDistance;
		float3 normal = normalize(geometry.vertices[0].normal * weights.x + geometry.vertices[1].normal * weights.y + geometry.vertices[2].normal * weights.z);
		if (bounce == 0u) {
			firstDistance = hitDistance;
			backface = dot(normal, direction) > 0.0f ? 1.0f : 0.0f;
		}
		if (dot(normal, direction) > 0.0f) normal = -normal;
		float2 uv = BakeTriangleUV(geometry, barycentric);
		if (material.normalTexture != 0xFFFFFFFFu) {
			// 通常描画と同じ接線符号でNormal Mapを評価する
			float3 tangent = DecodePackedDirection(geometry.vertices[0].tangentOct) * weights.x +
				DecodePackedDirection(geometry.vertices[1].tangentOct) * weights.y + DecodePackedDirection(geometry.vertices[2].tangentOct) * weights.z;
			tangent = normalize(tangent - normal * dot(normal, tangent));
			float3 bitangent = cross(normal, tangent) * geometry.vertices[0].tangentSign;
			float3 mapped = BakeTexture(material.normalTexture, uv).xyz * 2.0f - 1.0f;
			normal = normalize(mapped.x * tangent + mapped.y * bitangent + mapped.z * normal);
		}
		float3 albedo = saturate(material.baseColor.rgb * BakeTexture(material.baseColorTexture, uv).rgb);
		float metallic = material.metallic * BakeTexture(material.metallicTexture, uv).r * BakeTexture(material.metallicRoughnessTexture, uv).b;
		float3 emission = material.emissive.rgb * material.emissive.a * BakeTexture(material.emissiveTexture, uv).rgb;
		float3 diffuse = albedo * (1.0f - saturate(metallic)) * 0.96f;
		float roughness = material.roughness * BakeTexture(material.roughnessTexture, uv).r * BakeTexture(material.metallicRoughnessTexture, uv).g;
		bool capture = stage == 2u && bounce == 0u;
		float3 direct = BakeDirect(position, normal, seed, -direction, albedo, metallic, roughness, capture);
		result += throughput * (emission + (capture ? direct : diffuse * direct));
		throughput *= diffuse;
		if (max(throughput.x, max(throughput.y, throughput.z)) < 1e-4f) break;
		origin = position + normal * normalBias;
		direction = BakeHemisphere(normal, seed);
	}
	return max(result, 0.0f);
}
