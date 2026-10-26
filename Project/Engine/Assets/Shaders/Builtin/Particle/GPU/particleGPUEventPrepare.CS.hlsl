#include "particleGPUEvents.hlsli"

RWStructuredBuffer<ParticleGPUEvent> gGPUChildrenOut : register(u2, space4);
RWStructuredBuffer<uint> gGPUChildCountsOut : register(u3, space4);

[numthreads(256, 1, 1)]
void main(uint index : SV_DispatchThreadID) {
	if (eventOperation == 4u) {
		if (index < eventChildCapacity) {
			ParticleGPUInheritedState state = (ParticleGPUInheritedState) 0;
			state.color = 1.0f.xxxx;
			gGPUInheritedStates[index] = state;
		}
		return;
	}
	if (eventOperation == 0u) {
		if (index == 0) gGPUEventCounts[0] = 0;
		return;
	}
	if (eventOperation == 1u) {
		if (index == 0) gGPUChildCountsOut[0] = 0;
		return;
	}
	if (eventOperation == 3u) {
		if (index < 8) gGPUFeedbackStatistics[index] = 0;
		return;
	}
	if (index >= min(gGPUPreviousEventCounts[0], eventCapacity)) return;
	ParticleGPUEvent input = gGPUPreviousEvents[index];
	if ((input.flags & 192u) != 0u || !ParticleGPUIdentityEqual(input.target, ParticleGPUCurrentSource()) ||
		!ParticleGPUIdentityAlive(input.source)) return;
	uint count = ((input.flags >> 8u) & 255u) + 1u;
	uint base;
	InterlockedAdd(gGPUChildCountsOut[0], count, base);
	for (uint child = 0; child < count; ++child) {
		if (base + child >= eventChildCapacity) {
			InterlockedAdd(gGPUFeedbackStatistics[2], 1u);
			continue;
		}
		input.seed = ParticleHash(input.seed ^ child);
		gGPUChildrenOut[base + child] = input;
	}
}
