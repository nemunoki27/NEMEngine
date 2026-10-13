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

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID) {

	uint width, height;
	gDestColor.GetDimensions(width, height);
	uint2 pixel = dispatchID.xy;
	if (any(pixel >= uint2(width, height))) return;
	uint sourceWidth, sourceHeight;
	gSourcePosition.GetDimensions(sourceWidth, sourceHeight);
	float2 uv = (float2(pixel) + 0.5f) / float2(width, height);
	uint2 source = min(uint2(uv * float2(sourceWidth, sourceHeight)), uint2(sourceWidth, sourceHeight) - 1u);
	float3 position = gSourcePosition.Load(int3(source, 0)).xyz;
	float3 normal = DecodeGBufferNormal(gSourceNormal.Load(int3(source, 0)).xyz);
	bool surface = (gSourceFlags.Load(int3(source, 0)) & 1u) != 0u;
	float current = 1.0f;
	if (surface) {

		// 面の境界を越えずに近傍の遮蔽を平均する
		float sum = 0.0f;
		float weightSum = 0.0f;
		float positionLimit = max(radius * 0.15f, 0.002f);
		for (int y = -2; y <= 2; ++y) {
			for (int x = -2; x <= 2; ++x) {
				int2 samplePixel = clamp(int2(pixel) + int2(x, y), int2(0, 0), int2(width, height) - 1);
				uint2 sampleSource = min(uint2((float2(samplePixel) + 0.5f) / float2(width, height) *
					float2(sourceWidth, sourceHeight)), uint2(sourceWidth, sourceHeight) - 1u);
				float3 samplePosition = gSourcePosition.Load(int3(sampleSource, 0)).xyz;
				float3 sampleNormal = DecodeGBufferNormal(gSourceNormal.Load(int3(sampleSource, 0)).xyz);
				float weight = exp(-float(x * x + y * y) / 8.0f) * pow(saturate(dot(normal, sampleNormal)), 32.0f) *
					exp(-length(samplePosition - position) / positionLimit);
				if ((gSourceFlags.Load(int3(sampleSource, 0)) & 1u) == 0u) weight = 0.0f;
				sum += gSourceColor.Load(int3(samplePixel, 0)).r * weight;
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
