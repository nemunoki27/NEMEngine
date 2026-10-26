#ifndef NEM_PARTICLE_PAINT_READ
#define NEM_PARTICLE_PAINT_READ

#include "../../Common/descriptorHeapCompatibility.hlsli"

struct ParticlePaintReceiverGPU {
	float4x4 worldToReceiver;
	float4 scaleOpacity;
	uint2 entity;
	uint texture;
	uint projection;
};

StructuredBuffer<ParticlePaintReceiverGPU> gParticlePaintReceivers : register(t9, space4);
cbuffer ParticlePaintSceneConstants : register(b2, space4) {
	uint gParticlePaintReceiverCount;
	uint3 gParticlePaintPad;
};

uint FindParticlePaintReceiver(uint2 entity) {
	uint low = 0, high = gParticlePaintReceiverCount;
	while (low < high) {
		uint middle = low + (high - low) / 2;
		if (gParticlePaintReceivers[middle].entity.x < entity.x) low = middle + 1;
		else high = middle;
	}
	return low < gParticlePaintReceiverCount && all(gParticlePaintReceivers[low].entity == entity) ? low : 0xffffffffu;
}

// UVが重なる面は同じ実行時Textureを共有する
void ApplyParticlePaint(uint2 entity, float2 uv, float3 worldPosition, SamplerState samplerState, inout float4 baseColor) {
	uint index = FindParticlePaintReceiver(entity);
	if (index == 0xffffffffu) return;
	ParticlePaintReceiverGPU receiver = gParticlePaintReceivers[index];
	if (receiver.texture == 0xffffffffu) return;
	if (receiver.projection == 1u) {
		float3 local = mul(float4(worldPosition, 1), receiver.worldToReceiver).xyz;
		uv = local.xz * receiver.scaleOpacity.xy + 0.5f;
	}
	if (any(uv < 0) || any(uv > 1)) return;
	Texture2D<float4> image = NEM_TEXTURE2D(receiver.texture);
	float4 paint = image.SampleLevel(samplerState, uv, 0);
	baseColor.rgb = lerp(baseColor.rgb, paint.rgb, saturate(paint.a * receiver.scaleOpacity.z));
}

#endif
