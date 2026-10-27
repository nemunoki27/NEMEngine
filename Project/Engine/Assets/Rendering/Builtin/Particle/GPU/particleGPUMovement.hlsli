#ifndef NEM_PARTICLE_GPU_MOVEMENT
#define NEM_PARTICLE_GPU_MOVEMENT

// 大きい粒子番号でも開始角度の端数を失わないように加算する
float ParticleMovementPhase(float offset, uint birth) {
	if (offset == 0) return 0;
	float angle = 0;
	float step = fmod(offset, 360.0f);
	[unroll] for (uint bit = 0; bit < 32; ++bit) {
		if ((birth & (1u << bit)) != 0) angle = fmod(angle + step, 360.0f);
		step = fmod(step + step, 360.0f);
	}
	return angle;
}

float3 ParticleMovementOffset(ParticleGPUInstruction instruction, ParticleGPUCurveSample sample,
	float3 direction, uint birth) {
	float3 axis = ParticleNormalize(direction, float3(0, 1, 0));
	float3 reference = abs(axis.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
	float3 x = ParticleNormalize(cross(reference, axis), float3(1, 0, 0));
	float3 y = ParticleNormalize(cross(axis, x), float3(0, 0, 1));
	float start = instruction.parameter0.x + ParticleMovementPhase(instruction.parameter0.y, birth);
	float sign = instruction.flags != 0 ? -1.0f : 1.0f;
	if (instruction.operation == 12u) {
		float angle = radians(start + sample.value0.y * 360.0f * sign);
		return (x * cos(angle) + y * sin(angle)) * sample.value0.x;
	}
	float plane = radians(instruction.parameter0.z);
	float3 swing = x * cos(plane) + y * sin(plane);
	float phase = radians(start + sample.value0.z * 360.0f * sign);
	float angle = radians(sample.value0.y * sin(phase));
	return swing * (sin(angle) * sample.value0.x) + axis *
		((1.0f - cos(angle)) * sample.value0.x * instruction.parameter0.w);
}

#endif
