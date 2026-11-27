#ifndef NEM_RESTIR_DI_RESERVOIR_HLSLI
#define NEM_RESTIR_DI_RESERVOIR_HLSLI

// 前フレームの受光面と選択した光源を画素ごとに保持する
struct ReSTIRReservoir {
	uint lightIndex;
	uint sampleCount;
	float weight;
	float target;

	float3 position;
	uint flags;
	float2 normalXY;
	uint surfaceSignature;
	uint material;
};

float3 ReceiverNormal(ReSTIRReservoir surface) {
	float z = sqrt(saturate(1.0f - dot(surface.normalXY, surface.normalXY)));
	return float3(surface.normalXY, (surface.material & (1u << 30u)) != 0u ? -z : z);
}

uint2 PackReSTIRRadiance(float3 rgb, float history) {
	float4 value = clamp(float4(rgb, history), 0.0f, 65504.0f);
	uint4 packed = f32tof16(value);
	return packed.xy | (packed.zw << 16u);
}

float4 UnpackReSTIRRadiance(uint2 packed) {
	return f16tof32(uint4(packed & 0xFFFFu, packed >> 16u));
}

#endif
