#ifndef NEM_DESCRIPTOR_TABLE_COMPAT
#define NEM_DESCRIPTOR_TABLE_COMPAT 1
#endif
#include "../../Engine/Assets/Rendering/Builtin/Mesh/Common/pbrShading.hlsli"

RWStructuredBuffer<float4> gOutput : register(u0);

[numthreads(32, 1, 1)]
void main(uint3 thread : SV_DispatchThreadID) {

	uint outputIndex = thread.x;
	if (outputIndex >= 19u) return;
	uint index = outputIndex < 16u ? outputIndex : outputIndex == 16u ? 0u : outputIndex == 17u ? 12u : 18u;
	float metallic = outputIndex >= 16u && outputIndex < 18u ? 1.0f : 0.0f;
	RectLight light = (RectLight)0;
	light.color = 1.0f;
	light.pos = float3(0, 2, 0);
	light.direction = float3(0, -1, 0);
	light.right = float3(1, 0, 0);
	light.up = float3(0, 0, 1);
	light.sourceWidth = 2.0f;
	light.sourceHeight = 2.0f;
	light.intensity = 100.0f;
	light.attenuationRadius = 1000.0f;
	float3 position = 0.0f;
	float3 normal = float3(0, 1, 0);
	float3 view = normal;
	float roughness = 0.6f;
	if (index == 1u) light.pos.y = 4.0f;
	if (index == 2u) light.sourceWidth = light.sourceHeight = 4.0f;
	if (index == 3u) light.pos.y = 100.0f;
	if (index == 4u) { normal = float3(1, 0, 0); view = normalize(float3(1, 1, 0)); }
	if (index == 5u) { position.y = 3.0f; normal = view = float3(0, -1, 0); }
	if (index == 6u) light.sourceWidth = 0.0f;
	if (index == 7u) light.attenuationRadius = 0.0f;
	if (index == 8u) light.intensity = 0.0f;
	if (index == 9u) { light.pos = float3(-2, -2, 0); light.direction.y = 1.0f; view = normalize(float3(0.9f, 0.3f, 0)); }
	if (index == 10u) light.sourceWidth = -1.0f;
	if (index == 11u) { light.pos.y = 0.1f; roughness = 1.0f; }
	if (index == 12u) view = normalize(float3(0.9f, 0.3f, 0));
	if (index == 13u) { light.sourceWidth = 8.0f; light.sourceHeight = 0.5f; position = float3(2, 0, 1); }
	if (index == 14u) light.attenuationRadius = 1.0f;
	if (index == 15u) roughness = 0.35f;
	if (index == 18u) { normal = view = normalize(float3(1, 1, 1)); light.pos = float3(0.8f, 2, 0.8f);
		light.sourceWidth = light.sourceHeight = 4.0f; }

	// 実際の共通Shaderから拡散と鏡面を分けて取得する
	float3 f0 = metallic > 0.0f ? 1.0f.xxx : 0.04f.xxx;
	float specular = EvaluateRectAreaPBR(light, position, normal, view, 1.0f, metallic, roughness, f0, 0.0f).x;
	float diffuse = EvaluatePBRRectLight(light, position, normal, view, 1.0f, 0.0f, roughness, f0).x - specular;
	float diffuseReference = 0.0f;
	float specularReference = 0.0f;
	if (ComputeRectBarnAttenuation(light, position) > 0.0f) {

		// テストだけ高密度で積分し、4点近似への逆戻りを検出する
		for (uint y = 0u; y < 256u; ++y) {
			for (uint x = 0u; x < 256u; ++x) {

				float2 uv = (float2(x, y) + 0.5f) / 256.0f - 0.5f;
				float3 emitter = light.pos + light.right * uv.x * light.sourceWidth + light.up * uv.y * light.sourceHeight;
				float3 delta = emitter - position;
				float3 direction = normalize(delta);
				float irradiance = EvaluateRectSampleIrradiance(light, position, normal, emitter).x;
				float3 response = EvaluateRectAreaBRDF(normal, view, direction, 1.0f, 0.0f, roughness, f0);
				float diffuseResponse = (1.0f - f0.x) * (1.0f - metallic) / PI;
				diffuseReference += irradiance * diffuseResponse;
				specularReference += irradiance * (response.x - diffuseResponse);
			}
		}
	}
	gOutput[outputIndex] = float4(diffuse, diffuseReference / 65536.0f, specular, specularReference / 65536.0f);
}

// 旧4点近似との比較は検証Shaderだけで行う
float3 BenchmarkRectLight(uint index, bool useLTC) {

	RectLight light = (RectLight)0;
	light.color = 1.0f;
	light.pos = float3(0, 2, 0);
	light.direction = float3(0, -1, 0);
	light.right = float3(1, 0, 0);
	light.up = float3(0, 0, 1);
	light.sourceWidth = light.sourceHeight = 2.0f;
	light.intensity = 100.0f;
	light.attenuationRadius = 1000.0f;
	float3 position = float3(float(index % 128u) / 16.0f - 4.0f, 0, float(index / 128u) / 16.0f - 4.0f);
	float3 normal = float3(0, 1, 0);
	float3 view = normalize(float3(0, 4, -4) - position);
	if (useLTC) return EvaluatePBRRectLight(light, position, normal, view, 0.8f, 0.1f, 0.6f, 0.04f);
	const float2 offsets[4] = {float2(-0.375f, -0.125f), float2(0.125f, -0.375f),
		float2(-0.125f, 0.375f), float2(0.375f, 0.125f)};
	float attenuation = ComputeDistanceAttenuation(length(light.pos - position), light.attenuationRadius, 1.0f);
	float3 result = 0.0f;
	[unroll]
	for (uint sample = 0u; sample < 4u; ++sample) {

		float3 emitter = light.pos + light.right * offsets[sample].x * 2.0f + light.up * offsets[sample].y * 2.0f;
		float3 direction = normalize(emitter - position);
		float3 radiance = light.color.rgb * light.intensity * attenuation * saturate(dot(-direction, light.direction));
		result += EvaluatePBRRadiance(normal, view, direction, radiance, 0.8f, 0.1f, 0.6f, 0.04f);
	}
	return result * 0.25f;
}

[numthreads(64, 1, 1)]
void BenchmarkLTC(uint3 index : SV_DispatchThreadID) {

	gOutput[index.x] = float4(BenchmarkRectLight(index.x, true), 1.0f);
}

[numthreads(64, 1, 1)]
void BenchmarkFourPoints(uint3 index : SV_DispatchThreadID) {

	gOutput[index.x] = float4(BenchmarkRectLight(index.x, false), 1.0f);
}
