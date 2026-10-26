#ifndef NEM_PARTICLE_GPU_CURVE
#define NEM_PARTICLE_GPU_CURVE

struct ParticleGPUCurveSample { float4 value0; float4 value1; float4 timing; };

ParticleGPUCurveSample SampleParticleGPUCurve(StructuredBuffer<ParticleGPUCurveSample> curves,
	uint offset, uint count, float progress) {

	ParticleGPUCurveSample a = curves[offset];
	if (count <= 1u) return a;
	progress = saturate(progress);
	uint index;
	float blend;
	if (a.timing.y == 0) {
		float coordinate = progress * (count - 1);
		index = min((uint) coordinate, count - 2);
		blend = coordinate - index;
		a = curves[offset + index];
	} else {
		// 折り返しと段差を含む曲線だけ時刻から区間を探す
		uint low = 0, high = count;
		while (low < high) {
			uint middle = low + (high - low) / 2;
			if (curves[offset + middle].timing.x <= progress) low = middle + 1;
			else high = middle;
		}
		index = min(low == 0u ? 0u : low - 1u, count - 2);
		a = curves[offset + index];
		float end = curves[offset + index + 1].timing.x;
		blend = end > a.timing.x ? saturate((progress - a.timing.x) / (end - a.timing.x)) : 0;
	}
	ParticleGPUCurveSample b = curves[offset + index + 1];
	a.value0 = lerp(a.value0, b.value0, blend);
	a.value1 = lerp(a.value1, b.value1, blend);
	return a;
}

#endif
