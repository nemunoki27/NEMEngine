#include "particleGPUView.hlsli"

StructuredBuffer<uint2> gGPUViewKeysIn : register(t2);
RWStructuredBuffer<uint2> gGPUViewKeysOut : register(u0);
RWStructuredBuffer<uint> gGPUHistogram : register(u1);
RWStructuredBuffer<uint> gGPUViewCounts : register(u2);
RWStructuredBuffer<uint> gGPUHistogramOffsets : register(u3);
RWStructuredBuffer<uint> gGPUIndirectArgs : register(u4);

groupshared uint localCounts[16];
groupshared uint localMasks[128];

bool ParticleVisible(ParticleAttributesGPU particle, float3 position) {
	float3 scale = particle.scaleSize.xyz;
	if (particle.hasParent != 0) scale *= gGPUParents[particle.phase].scale.xyz;
	float radius = length(scale) * abs(particle.scaleSize.w) * geometryRadius;
	for (uint i = 0; i < 6; ++i) {
		if (dot(frustum[i], float4(position, 1)) < -radius) return false;
	}
	return true;
}

[numthreads(256, 1, 1)]
void main(uint index : SV_DispatchThreadID, uint localIndex : SV_GroupIndex, uint3 group : SV_GroupID) {
	if (operation == 0u) {
		if (index < capacity) gGPUViewKeysOut[index] = uint2(0xffffffffu, 0xffffffffu);
		if (index < phaseCount * 2 + 4) gGPUViewCounts[index] = 0;
		return;
	}
	if (operation == 1u) {
		if (index >= capacity) return;
		ParticleAttributesGPU particle = gGPUParticles[index];
		if (particle.alive == 0) return;
		float3 position = ParticleWorldPosition(particle, gGPUParents[particle.phase]);
		if (!ParticleVisible(particle, position)) return;
		uint compact;
		InterlockedAdd(gGPUViewCounts[phaseCount * 2], 1u, compact);
		InterlockedAdd(gGPUViewCounts[particle.phase], 1u);
		float depth = max(dot(position - cameraPosition.xyz, cameraForward.xyz), 0.0f);
		uint key = particle.phase << 24;
		if (sortEnabled != 0) key |= 0x00ffffffu - (asuint(depth) >> 8);
		gGPUViewKeysOut[compact] = uint2(key, index);
		return;
	}
	if (operation == 2u) {
		if (index != 0) return;
		uint start = 0;
		for (uint phase = 0; phase < phaseCount; ++phase) {
			gGPUViewCounts[phaseCount + phase] = start;
			start += gGPUViewCounts[phase];
		}
		return;
	}
	if (operation == 3u) {
		if (localIndex < 16) localCounts[localIndex] = 0;
		GroupMemoryBarrierWithGroupSync();
		if (index < capacity) InterlockedAdd(localCounts[(gGPUViewKeysIn[index].x >> radixShift) & 15u], 1u);
		GroupMemoryBarrierWithGroupSync();
		if (localIndex < 16) gGPUHistogram[group.x * 16 + localIndex] = localCounts[localIndex];
		return;
	}
	if (operation == 4u) {
		if (index >= 16) return;
		uint prefix = 0;
		for (uint bin = 0; bin < index; ++bin)
			for (uint block = 0; block < groupCount; ++block) prefix += gGPUHistogram[block * 16 + bin];
		for (uint block = 0; block < groupCount; ++block) {
			gGPUHistogramOffsets[block * 16 + index] = prefix;
			prefix += gGPUHistogram[block * 16 + index];
		}
		return;
	}
	if (operation == 5u) {
		uint2 value = index < capacity ? gGPUViewKeysIn[index] : uint2(0xffffffffu, 0xffffffffu);
		uint digit = (value.x >> radixShift) & 15u;
		if (localIndex < 128) localMasks[localIndex] = 0;
		GroupMemoryBarrierWithGroupSync();
		InterlockedOr(localMasks[digit * 8 + localIndex / 32], 1u << (localIndex % 32));
		GroupMemoryBarrierWithGroupSync();
		if (index >= capacity) return;
		// Group内の順序を固定し、Radixの各段で同じ桁の順序を保つ
		uint localPrefix = 0;
		for (uint i = 0; i < localIndex / 32; ++i) localPrefix += countbits(localMasks[digit * 8 + i]);
		localPrefix += countbits(localMasks[digit * 8 + localIndex / 32] & ((1u << (localIndex % 32)) - 1));
		uint destination = gGPUHistogramOffsets[group.x * 16 + digit] + localPrefix;
		gGPUViewKeysOut[destination] = value;
		return;
	}
	if (operation == 6u && index < phaseCount) {
		uint offset = index * 5;
		gGPUIndirectArgs[offset] = indexCount;
		gGPUIndirectArgs[offset + 1] = gGPUViewCounts[index];
		gGPUIndirectArgs[offset + 2] = 0;
		gGPUIndirectArgs[offset + 3] = 0;
		gGPUIndirectArgs[offset + 4] = 0;
	}
}
