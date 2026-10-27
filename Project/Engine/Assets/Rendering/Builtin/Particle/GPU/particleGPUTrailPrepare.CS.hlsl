#include "particleGPUView.hlsli"
#define NEM_TRAIL_CONSTANT_REGISTER b1
#include "particleGPUTrail.hlsli"
#include "particleGPUTrailRead.hlsli"

RWStructuredBuffer<uint2> gGPUTrailSegments : register(u0);
RWStructuredBuffer<uint4> gGPUTrailArguments : register(u1);

[numthreads(256, 1, 1)]
void main(uint index : SV_DispatchThreadID) {
	if (trailOperation == 0u) {
		if (index == 0) gGPUTrailArguments[0] = uint4(6, 0, 0, 0);
		if (sortEnabled != 0 && index < trailCapacity * pointCapacity) gGPUTrailSegments[index] = 0xffffffffu.xx;
		return;
	}
	if (index >= trailCapacity) return;
	ParticleTrailGPU trail = gGPUTrails[index];
	uint count = ParticleTrailPointCount(trail);
	if (trail.active == 0 || count < 2) return;
	float3 minimum = 1e30f.xxx;
	float3 maximum = -1e30f.xxx;
	for (uint i = 0; i < count; ++i) {
		float3 position = ReadParticleTrailPoint(trail, index, i).positionAge.xyz;
		minimum = min(minimum, position);
		maximum = max(maximum, position);
	}
	float3 center = (minimum + maximum) * 0.5f;
	float radius = length(maximum - minimum) * 0.5f + max(abs(startWidth), abs(endWidth));
	for (uint plane = 0; plane < 6; ++plane)
		if (dot(frustum[plane], float4(center, 1)) < -radius) return;
	uint offset;
	InterlockedAdd(gGPUTrailArguments[0].y, count - 1, offset);
	for (uint i = 0; i + 1 < count; ++i) {
		float3 position = ReadParticleTrailPoint(trail, index, i).positionAge.xyz;
		float depth = max(dot(position - cameraPosition.xyz, cameraForward.xyz), 0.0f);
		uint key = sortEnabled != 0 ? 0x00ffffffu - (asuint(depth) >> 8) : 0u;
		uint segment = index * pointCapacity + i;
		// 加算描画は可視セグメントを詰め、奥行きソートを省く
		gGPUTrailSegments[sortEnabled != 0 ? segment : offset + i] = uint2(key, segment);
	}
}
