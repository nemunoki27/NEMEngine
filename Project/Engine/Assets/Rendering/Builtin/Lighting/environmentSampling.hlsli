#ifndef NEM_ENVIRONMENT_SAMPLING_HLSLI
#define NEM_ENVIRONMENT_SAMPLING_HLSLI

static const float kEnvironmentPI = 3.14159265359f;

// 固定系列で積分位置を分散する
float2 EnvironmentHammersley(uint index, uint count) {

	return float2(float(index) / float(count), float(reversebits(index)) * 2.3283064365386963e-10f);
}

// GGX分布から半角方向を選ぶ
float3 EnvironmentSampleGGX(float2 sampleUV, float3 normal, float roughness) {

	float alpha = max(roughness * roughness, 0.0001f);
	float phi = 2.0f * kEnvironmentPI * sampleUV.x;
	float cosTheta = sqrt((1.0f - sampleUV.y) / (1.0f + (alpha * alpha - 1.0f) * sampleUV.y));
	float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
	float3 up = abs(normal.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
	float3 tangent = normalize(cross(up, normal));
	return normalize(tangent * cos(phi) * sinTheta + cross(normal, tangent) * sin(phi) * sinTheta + normal * cosTheta);
}

// D3Dの面順で方向を復元する
float3 EnvironmentCubeDirection(uint face, float2 uv) {

	float2 st = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
	if (face == 0) return normalize(float3(1, st.y, -st.x));
	if (face == 1) return normalize(float3(-1, st.y, st.x));
	if (face == 2) return normalize(float3(st.x, 1, -st.y));
	if (face == 3) return normalize(float3(st.x, -1, st.y));
	if (face == 4) return normalize(float3(st.x, st.y, 1));
	return normalize(float3(-st.x, st.y, -1));
}

#endif
