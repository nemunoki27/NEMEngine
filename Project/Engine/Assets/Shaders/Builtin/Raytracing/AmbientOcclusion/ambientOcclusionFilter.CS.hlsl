//============================================================================
//	include
//============================================================================
#include "../ReflectionFilter/reflectionFilterCommon.hlsli"

cbuffer PostProcessParameters : register(b1) {

	float radius;
	float3 _pad;
};

Texture2D<float4> gSourceColor : register(t0);
Texture2D<float4> gSourcePosition : register(t1);
Texture2D<float4> gSourceNormal : register(t2);
Texture2D<float2> gSourceMotion : register(t3);
Texture2D<uint> gSourceFlags : register(t4);
Texture2D<float4> gHistoryColor : register(t5);
Texture2D<float4> gHistoryGeometry : register(t6);
Texture2D<float2> gHistoryNormal : register(t7);
RWTexture2D<float4> gDestColor : register(u0);
RWTexture2D<float4> gHistoryGeometryOutput : register(u1);
RWTexture2D<float2> gHistoryNormalOutput : register(u2);

groupshared float3 sharedPosition[144];
groupshared float3 sharedNormal[144];
groupshared float sharedVisibility[144];
groupshared uint sharedFlags[144];

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID, uint3 groupID : SV_GroupID,
	uint3 groupThread : SV_GroupThreadID, uint groupIndex : SV_GroupIndex) {

	uint width, height;
	gDestColor.GetDimensions(width, height);
	uint sourceWidth, sourceHeight;
	gSourcePosition.GetDimensions(sourceWidth, sourceHeight);
	// 周囲二画素の入力をGroup内で共有する
	for (uint index = groupIndex; index < 144u; index += 64u) {
		int2 samplePixel = clamp(int2(groupID.xy * 8u) + int2(index % 12u, index / 12u) - 2,
			int2(0, 0), int2(width, height) - 1);
		uint2 sampleSource = min(uint2((float2(samplePixel) + 0.5f) / float2(width, height) *
			float2(sourceWidth, sourceHeight)), uint2(sourceWidth, sourceHeight) - 1u);
		sharedPosition[index] = gSourcePosition.Load(int3(sampleSource, 0)).xyz;
		sharedNormal[index] = DecodeGBufferNormal(gSourceNormal.Load(int3(sampleSource, 0)).xyz);
		sharedVisibility[index] = gSourceColor.Load(int3(samplePixel, 0)).r;
		sharedFlags[index] = gSourceFlags.Load(int3(sampleSource, 0));
	}
	GroupMemoryBarrierWithGroupSync();
	uint2 pixel = dispatchID.xy;
	if (any(pixel >= uint2(width, height))) return;
	float2 uv = (float2(pixel) + 0.5f) / float2(width, height);
	uint2 source = min(uint2(uv * float2(sourceWidth, sourceHeight)), uint2(sourceWidth, sourceHeight) - 1u);
	uint centerIndex = (groupThread.y + 2u) * 12u + groupThread.x + 2u;
	float3 position = sharedPosition[centerIndex];
	float3 normal = sharedNormal[centerIndex];
	bool surface = (sharedFlags[centerIndex] & 1u) != 0u;
	float current = 1.0f;
	if (surface) {

		// 面の境界を越えずに近傍の遮蔽を平均する
		float sum = 0.0f;
		float weightSum = 0.0f;
		float positionLimit = max(radius * 0.15f, 0.002f);
		for (int y = -2; y <= 2; ++y) {
			for (int x = -2; x <= 2; ++x) {
				uint sampleIndex = uint(int(centerIndex) + y * 12 + x);
				float3 samplePosition = sharedPosition[sampleIndex];
				float3 sampleNormal = sharedNormal[sampleIndex];
				float weight = exp(-float(x * x + y * y) / 8.0f) * pow(saturate(dot(normal, sampleNormal)), 32.0f) *
					exp(-length(samplePosition - position) / positionLimit);
				if ((sharedFlags[sampleIndex] & 1u) == 0u) weight = 0.0f;
				sum += sharedVisibility[sampleIndex] * weight;
				weightSum += weight;
			}
		}
		current = sum / max(weightSum, 0.0001f);
		// 前frameの位置と法線が一致する履歴だけを使う
		float2 previousUV = uv - gSourceMotion.Load(int3(source, 0));
		if (all(previousUV >= 0.0f.xx) && all(previousUV < 1.0f.xx)) {
			uint2 historyPixel = uint2(previousUV * float2(width, height));
			float4 geometry = gHistoryGeometry.Load(int3(historyPixel, 0));
			float3 previousNormal = DecodeOctahedralNormal(gHistoryNormal.Load(int3(historyPixel, 0)));
			if (geometry.w > 0.0f && distance(geometry.xyz, position) < max(radius * 0.02f, 0.002f) &&
				dot(previousNormal, normal) > 0.95f) {
				float history = gHistoryColor.Load(int3(historyPixel, 0)).r;
				current = lerp(current, clamp(history, current - 0.15f, current + 0.15f), 0.85f);
			}
		}
	}
	gDestColor[pixel] = float4(saturate(current).xxx, 1.0f);
	gHistoryGeometryOutput[pixel] = float4(position, surface ? 1.0f : 0.0f);
	gHistoryNormalOutput[pixel] = EncodeOctahedralNormal(normal);
}
