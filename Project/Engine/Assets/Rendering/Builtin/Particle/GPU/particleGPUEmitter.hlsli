#ifndef NEM_PARTICLE_GPU_EMITTER
#define NEM_PARTICLE_GPU_EMITTER

void ParticleEmitter(uint claim, uint randomSeed, out float3 position, out float3 direction) {
	uint random = randomSeed;
	direction = ParticleNormalize(ParticleRandom3(random) * 2 - 1, float3(0, 1, 0));
	position = direction * shapeParameters.x;
	if (shape == 1u) {
		direction.y = abs(direction.y);
		position = direction * shapeParameters.x;
	} else if (shape == 2u || shape == 7u) {
		uint bits = faceMask;
		uint count = countbits(bits);
		uint pick = min((uint) (ParticleRandom(random) * count), count - 1);
		uint face = 0;
		for (uint i = 0; i < 6; ++i) {
			if ((bits & (1u << i)) == 0) continue;
			if (pick-- == 0) { face = i; break; }
		}
		position = (ParticleRandom3(random) - 0.5f) * shapeSize.xyz;
		direction = 0;
		uint axis = face / 2;
		direction[axis] = (face & 1u) == 0 ? 1 : -1;
		position[axis] = direction[axis] * shapeSize[axis] * 0.5f;
		if (shape == 7u) position.z = 0;
	} else if (shape == 3u) {
		float mainAngle = ParticleRandom(random) * 6.28318530718f;
		float tubeAngle = ParticleRandom(random) * 6.28318530718f;
		float radius = ParticleRandom(random) * shapeSize.w;
		float3 radial = float3(cos(mainAngle), 0, sin(mainAngle));
		direction = radial * cos(tubeAngle) + float3(0, sin(tubeAngle), 0);
		position = radial * shapeParameters.w + direction * radius;
	} else if (shape == 4u) {
		float span = abs(circle.y - circle.x) >= 359.999f ? 360.0f : fmod(circle.y - circle.x + 720.0f, 360.0f);
		float offset = ParticleRandom(random) * span;
		if (spawnMode == 1u) offset = (claim + 0.5f) * span / max(spawnCount, 1u);
		if (spawnMode == 2u) offset = fmod(abs(circle.z) * float(birthLow + claim), max(span, 0.001f));
		float base = circle.w < 0 ? circle.y : circle.x;
		float angle = radians(base + offset * circle.w);
		direction = speedTime.w != 0 ? float3(cos(angle), sin(angle), 0) : float3(cos(angle), 0, sin(angle));
		position = direction * shapeParameters.x;
		if (velocityMode != 0) {
			float sign = circle.w * (velocityMode == 1u ? 1 : -1);
			direction = speedTime.w != 0 ? float3(-sin(angle), cos(angle), 0) * sign : float3(-sin(angle), 0, cos(angle)) * sign;
		}
	} else if (shape == 5u) {
		float angle = ParticleRandom(random) * 6.28318530718f;
		position = float3(cos(angle), 0, sin(angle)) * ParticleRandom(random) * shapeParameters.z;
		float coneAngle = radians(shapeParameters.y);
		direction = coneAngle > 0.001f && shapeParameters.z > 0.001f ?
			normalize(position + float3(0, shapeParameters.z / tan(coneAngle), 0)) : float3(0, 1, 0);
	} else if (shape == 6u) {
		position = 0;
		direction = ParticleNormalize(pointDirection.xyz, float3(0, 1, 0));
	} else if (shape == 8u) {
		position = float3((ParticleRandom(random) * 2 - 1) * shapeParameters.z, 0, 0);
		float angle = radians((ParticleRandom(random) * 2 - 1) * shapeParameters.y);
		direction = float3(sin(angle), cos(angle), 0);
	}
	position += lerp(offsetMinimum.xyz, offsetMaximum.xyz, ParticleRandom3(random));
}

#endif
