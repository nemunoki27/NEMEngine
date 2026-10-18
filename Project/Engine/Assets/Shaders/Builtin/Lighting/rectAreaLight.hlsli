#ifndef NEM_RECT_AREA_LIGHT_HLSLI
#define NEM_RECT_AREA_LIGHT_HLSLI

// Copyright (c) 2017, Eric Heitz, Jonathan Dupuy, Stephen Hill and David Neubelt
// Real-Time Polygonal-Light Shading with Linearly Transformed Cosines, SIGGRAPH 2016
// 出典と利用条件はTextures/Lighting/LTC/SOURCE.jsonとLICENSE.txtを参照
#include "../Common/pbrMath.hlsli"
#include "../Common/descriptorHeapCompatibility.hlsli"

// MaterialのSamplerに依存せずLUTを線形補間する
float4 SampleRectLTCTable(Texture2D<float4> table, float2 uv) {

	float2 coordinate = saturate(uv) * 63.0f;
	int2 first = int2(coordinate);
	int2 second = min(first + 1, 63);
	float2 weight = frac(coordinate);
	float4 lower = lerp(table.Load(int3(first, 0)), table.Load(int3(second.x, first.y, 0)), weight.x);
	float4 upper = lerp(table.Load(int3(first.x, second.y, 0)), table.Load(int3(second, 0)), weight.x);
	return lerp(lower, upper, weight.y);
}

// 球面多角形の辺を積分する
float RectLTCIntegrateEdge(float3 first, float3 second) {

	float cosine = clamp(dot(first, second), -1.0f, 1.0f);
	float x = abs(cosine);
	float numerator = 0.8543985f + (0.4965155f + 0.0145206f * x) * x;
	float denominator = 3.4175940f + (4.1616724f + x) * x;
	float ratio = numerator / denominator;
	float angle = cosine > 0.0f ? ratio : 0.5f * rsqrt(max(1.0f - cosine * cosine, 1e-7f)) - ratio;
	return cross(first, second).z * angle;
}

// 凸多角形を受光側の半球へ切り取る
uint ClipRectLTCHorizon(float3 corners[8], uint vertexCount, out float3 polygon[8]) {

	uint count = 0u;
	[loop]
	for (uint index = 0u; index < vertexCount; ++index) {

		float3 first = corners[index];
		float3 second = corners[(index + 1u) % vertexCount];
		bool firstVisible = first.z > 0.0f;
		bool secondVisible = second.z > 0.0f;
		if (firstVisible) polygon[count++] = first;
		if (firstVisible != secondVisible) {
			polygon[count++] = lerp(first, second, first.z / (first.z - second.z));
		}
	}
	return count;
}

// 単位球上へ射影して多角形を積分する
float RectLTCIntegral(float3 polygon[8], uint count) {

	if (count < 3u) return 0.0f;
	[loop]
	for (uint vertex = 0u; vertex < count; ++vertex) {
		polygon[vertex] *= rsqrt(max(dot(polygon[vertex], polygon[vertex]), 1e-12f));
	}
	float integral = 0.0f;
	[loop]
	for (uint edge = 0u; edge < count; ++edge) {
		integral += RectLTCIntegrateEdge(polygon[edge], polygon[(edge + 1u) % count]);
	}
	return saturate(abs(integral));
}

// 拡散と鏡面の両方を矩形全体から評価する
float3 EvaluateRectAreaPBR(RectLight light, float3 worldPos, float3 N, float3 V,
	float3 albedo, float metallic, float roughness, float3 F0, float diffuseWeight = 1.0f) {

	float attenuation = ComputeRectRangeAttenuation(light, worldPos) * ComputeRectBarnAttenuation(light, worldPos);
	if (attenuation <= 0.0f || light.sourceWidth <= 0.0f || light.sourceHeight <= 0.0f ||
		rectLTCMatrixIndex == 0xFFFFFFFFu || rectLTCAmplitudeIndex == 0xFFFFFFFFu) return 0.0f;

	// 視線と法線が平行な場合も直交基底を保つ
	float nDotV = saturate(dot(N, V));
	float3 tangent = V - N * dot(N, V);
	if (dot(tangent, tangent) < 1e-8f) {
		tangent = cross(abs(N.z) < 0.999f ? float3(0, 0, 1) : float3(0, 1, 0), N);
	}
	tangent = normalize(tangent);
	float3 bitangent = cross(N, tangent);
	float3 corners[8];
	float3 visibleCorners[8];
	float3 specularCorners[8];

	// 参照テーブルと同じ粗さと視線角の座標を使う
	float2 uv = float2(clamp(roughness, 0.04f, 1.0f), sqrt(1.0f - nDotV));
	Texture2D<float4> matrixTable = NEM_TEXTURE2D(rectLTCMatrixIndex);
	Texture2D<float4> amplitudeTable = NEM_TEXTURE2D(rectLTCAmplitudeIndex);
	float4 coefficients = SampleRectLTCTable(matrixTable, uv);
	float2 amplitude = SampleRectLTCTable(amplitudeTable, uv).xy;

	[unroll]
	for (uint index = 0u; index < 4u; ++index) {

		float2 sign = float2(index == 1u || index == 2u ? 1.0f : -1.0f, index >= 2u ? 1.0f : -1.0f);
		float3 delta = light.pos + sign.x * light.right * light.sourceWidth * 0.5f +
			sign.y * light.up * light.sourceHeight * 0.5f - worldPos;
		float3 local = float3(dot(delta, tangent), dot(delta, bitangent), dot(delta, N));
		corners[index] = local;
	}
	// 元の受光面で先に切り取り、鏡面ローブからの裏面漏れを防ぐ
	uint visibleCount = ClipRectLTCHorizon(corners, 4u, visibleCorners);
	float diffuse = RectLTCIntegral(visibleCorners, visibleCount);
	[loop]
	for (uint vertex = 0u; vertex < visibleCount; ++vertex) {

		float3 local = visibleCorners[vertex];
		// GLSL参照実装の列優先行列を明示して変換する
		specularCorners[vertex] = float3(coefficients.x * local.x + coefficients.z * local.z, local.y,
			coefficients.y * local.x + coefficients.w * local.z);
	}
	uint specularCount = ClipRectLTCHorizon(specularCorners, visibleCount, corners);
	float specular = RectLTCIntegral(corners, specularCount);
	float3 diffuseColor = (1.0f - F0) * (1.0f - saturate(metallic)) * albedo;
	float3 specularColor = F0 * amplitude.x + (1.0f - F0) * amplitude.y;
	return GetRectLightRadiance(light) * attenuation *
		(diffuseColor * diffuse * diffuseWeight + specularColor * specular);
}

// 参照積分はLTCテーブルと同じGGX遮蔽を使う
float3 EvaluateRectAreaBRDF(float3 N, float3 V, float3 L, float3 albedo,
	float metallic, float roughness, float3 F0) {

	float nDotV = saturate(dot(N, V));
	float nDotL = saturate(dot(N, L));
	if (nDotV <= 0.0f || nDotL <= 0.0f) return 0.0f;
	float3 halfVector = normalize(V + L);
	float alpha = max(roughness, 0.04f);
	alpha *= alpha;
	float lambdaV = 0.5f * (sqrt(1.0f + alpha * alpha * (1.0f - nDotV * nDotV) / (nDotV * nDotV)) - 1.0f);
	float lambdaL = 0.5f * (sqrt(1.0f + alpha * alpha * (1.0f - nDotL * nDotL) / (nDotL * nDotL)) - 1.0f);
	float visibility = 1.0f / (1.0f + lambdaV + lambdaL);
	float3 fresnel = FresnelSchlick(saturate(dot(V, halfVector)), F0);
	float nDotH = saturate(dot(N, halfVector));
	float denominator = nDotH * nDotH * (alpha * alpha - 1.0f) + 1.0f;
	float distribution = alpha * alpha / max(PI * denominator * denominator, 1e-12f);
	float3 specular = distribution * visibility * fresnel /
		max(4.0f * nDotV * nDotL, 1e-7f);
	return (1.0f - F0) * (1.0f - metallic) * albedo / PI + specular;
}

#endif
