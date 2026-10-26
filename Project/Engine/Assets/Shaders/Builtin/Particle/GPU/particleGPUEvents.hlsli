#ifndef NEM_PARTICLE_GPU_EVENTS
#define NEM_PARTICLE_GPU_EVENTS

#include "particleGPUShared.hlsli"

struct ParticleGPUIdentity {
	uint2 entity;
	uint2 token;
	uint2 epoch;
};

struct ParticleGPUEvent {
	ParticleGPUIdentity source;
	ParticleGPUIdentity target;
	float4 positionRadius;
	float4 normalHardness;
	float4 velocityLifetime;
	float4 color;
	float2 uv;
	uint seed;
	uint flags;
};

struct ParticleGPUEventRoute {
	ParticleGPUIdentity target;
	uint kind;
	uint phase;
	uint count;
	uint flags;
	float probability;
	float time;
	float4 color;
	float4 parameters;
};

struct ParticleGPUInheritedState {
	float4 color;
	uint seed;
	uint flags;
	uint2 reserved;
};

cbuffer ParticleGPUEventConstants : register(b0, space4) {
	uint2 eventSourceEntity;
	uint2 eventSourceToken;
	uint2 eventSourceEpoch;
	uint eventRouteCount;
	uint eventCapacity;
	uint eventSystemCount;
	uint eventChildCapacity;
	uint eventOperation;
	uint eventReserved;
};

StructuredBuffer<ParticleGPUEventRoute> gGPUEventRoutes : register(t0, space4);
StructuredBuffer<ParticleGPUIdentity> gGPUEventSystems : register(t1, space4);
StructuredBuffer<ParticleGPUEvent> gGPUPreviousEvents : register(t2, space4);
StructuredBuffer<uint> gGPUPreviousEventCounts : register(t3, space4);
StructuredBuffer<ParticleGPUEvent> gGPUChildren : register(t4, space4);
StructuredBuffer<uint> gGPUChildCounts : register(t5, space4);
RWStructuredBuffer<ParticleGPUEvent> gGPUEvents : register(u0, space4);
RWStructuredBuffer<uint> gGPUEventCounts : register(u1, space4);
RWStructuredBuffer<uint> gGPUFeedbackStatistics : register(u4, space4);
RWStructuredBuffer<ParticleGPUInheritedState> gGPUInheritedStates : register(u11, space4);

struct ParticleGPUImpact {
	float3 position;
	float3 normal;
	float3 velocity;
	uint2 receiver;
	float2 uv;
	uint hasUV;
};

ParticleGPUIdentity ParticleGPUCurrentSource() {
	ParticleGPUIdentity result;
	result.entity = eventSourceEntity;
	result.token = eventSourceToken;
	result.epoch = eventSourceEpoch;
	return result;
}

bool ParticleGPUIdentityEqual(ParticleGPUIdentity a, ParticleGPUIdentity b) {
	return all(a.entity == b.entity) && all(a.token == b.token) && all(a.epoch == b.epoch);
}

bool ParticleGPUIdentityAlive(ParticleGPUIdentity identity) {
	uint low = 0, high = eventSystemCount;
	while (low < high) {
		uint middle = low + (high - low) / 2;
		if (gGPUEventSystems[middle].entity.x < identity.entity.x) low = middle + 1;
		else high = middle;
	}
	return low < eventSystemCount && ParticleGPUIdentityEqual(gGPUEventSystems[low], identity);
}

// 出力は上限で切り捨て、次回更新まで読み取り側へ渡さない
void ParticleGPUEmitEvent(uint kind, uint particleIndex, ParticleAttributesGPU particle,
	ParticleGPUImpact impact, float previousAge) {
	if (eventRouteCount == 0u) return;
	uint ordinal = 0;
	if (eventReserved != 0u) {
		ParticleGPUInheritedState state = gGPUInheritedStates[particleIndex];
		ordinal = state.reserved.x;
		state.reserved.x = ordinal + 1u;
		gGPUInheritedStates[particleIndex] = state;
	}
	uint moment = ParticleHash(kind ^ ParticleHash(particle.phase) ^ asuint(particle.positionAge.w));
	moment ^= ParticleHash(ordinal);
	moment ^= ParticleHash(asuint(impact.position.x) ^ ParticleHash(asuint(impact.position.y)) ^ asuint(impact.position.z));
	for (uint routeIndex = 0; routeIndex < eventRouteCount; ++routeIndex) {
		ParticleGPUEventRoute route = gGPUEventRoutes[routeIndex];
		if (route.kind != kind || (route.phase != 0xffffffffu && route.phase != particle.phase)) continue;
		if (kind == 2u && !(previousAge < route.time && particle.positionAge.w >= route.time)) continue;
		uint random = ParticleHash(particle.birthLow ^ ParticleHash(particle.birthHigh) ^ ParticleHash(routeIndex) ^ particleIndex ^ moment);
		if (ParticleRandom(random) >= route.probability) continue;
		uint claim;
		InterlockedAdd(gGPUEventCounts[0], 1u, claim);
		if (claim >= eventCapacity) {
			InterlockedAdd(gGPUFeedbackStatistics[1], 1u);
			continue;
		}
		ParticleGPUEvent output = (ParticleGPUEvent) 0;
		output.source = ParticleGPUCurrentSource();
		output.target = route.target;
		output.positionRadius = float4(impact.position, route.parameters.x);
		output.normalHardness = float4(impact.normal, route.parameters.y);
		output.velocityLifetime = float4(impact.velocity, route.parameters.z);
		output.color = particle.color * route.color;
		output.uv = impact.uv;
		output.seed = random;
		output.flags = route.flags | ((min(route.count, 256u) - 1u) << 8u) | (impact.hasUV != 0 ? 32u : 0u);
		if ((route.flags & 192u) != 0u) output.target.entity = impact.receiver;
		gGPUEvents[claim] = output;
		InterlockedAdd(gGPUFeedbackStatistics[0], 1u);
	}
}

uint ParticleGPUChildCount() {
	return min(gGPUChildCounts[0], eventChildCapacity);
}

// 継承はWorld空間で適用してから既存の親空間へ変換する
void ParticleGPUApplyChild(inout ParticleAttributesGPU particle, ParticleGPUEvent child) {
	if ((child.flags & 1u) != 0u) particle.positionAge.xyz = child.positionRadius.xyz;
	if ((child.flags & 2u) != 0u) particle.rotation = ParticleLookRotation(child.normalHardness.xyz);
	if ((child.flags & 4u) != 0u) particle.velocityLifetime.xyz = child.velocityLifetime.xyz;
}

void ParticleGPUPrepareInheritedState(uint index, bool isChild, ParticleGPUEvent child) {
	if (eventReserved == 0u) return;
	ParticleGPUInheritedState state = (ParticleGPUInheritedState) 0;
	state.color = 1.0f.xxxx;
	if (isChild) {
		state.color = child.color;
		state.seed = child.seed;
		state.flags = child.flags & 24u;
	}
	gGPUInheritedStates[index] = state;
}

uint ParticleGPUSeed(uint index, uint fallback) {
	if (eventReserved == 0u) return fallback;
	ParticleGPUInheritedState state = gGPUInheritedStates[index];
	return (state.flags & 16u) != 0u ? state.seed : fallback;
}

#ifdef NEM_PARTICLE_GPU_MODULES
void ParticleGPUApplyInheritedColor(inout ParticleAttributesGPU particle, uint index) {
	if (eventReserved == 0u) return;
	ParticleGPUInheritedState state = gGPUInheritedStates[index];
	if ((state.flags & 8u) == 0u) return;
	ParticleGPUPhase phase = gGPUPhases[particle.phase];
	bool freshColor = false;
	for (uint offset = 0; offset < phase.instructionCount; ++offset) {
		if (gGPUInstructions[phase.instructionOffset + offset].operation == 0u) {
			freshColor = true;
			break;
		}
	}
	if (freshColor || (state.flags & 256u) == 0u) {
		particle.color *= state.color;
		for (uint offset = 0; offset < phase.instructionCount; ++offset) {
			ParticleGPUInstruction instruction = gGPUInstructions[phase.instructionOffset + offset];
			if (instruction.operation == 3u && (instruction.flags & 1u) != 0u) particle.emissive.xyz *= state.color.rgb;
		}
	}
	state.flags |= 256u;
	gGPUInheritedStates[index] = state;
}
#endif

#endif
