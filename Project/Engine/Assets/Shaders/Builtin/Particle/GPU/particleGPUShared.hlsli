#ifndef NEM_PARTICLE_GPU_SHARED
#define NEM_PARTICLE_GPU_SHARED

struct ParticleAttributesGPU {
	float4 positionAge;
	float4 velocityLifetime;
	float4 rotation;
	float4 color;
	float4 scaleSize;
	float4 emissive;
	float4 uv;
	float4 uvRotationPivotAlpha;
	uint alive;
	uint phase;
	uint birthLow;
	uint birthHigh;
	uint hasParent;
	uint parentPhase;
	uint trailIndex;
	uint trailGeneration;
};

struct ParticleParentGPU {
	float4x4 matrix;
	float4x4 inverse;
	float4 rotation;
	float4 scale;
	float4x4 previousMatrix;
	float4 previousRotation;
	float4 previousScale;
	uint valid;
	uint previousValid;
	uint keepWorldOnDetach;
	uint reserved;
};

struct ParticleGPUInstruction {
	uint operation;
	uint curveOffset;
	uint curveCount;
	uint flags;
	float4 parameter0;
	float4 parameter1;
	float4 parameter2;
	float4 parameter3;
};

struct ParticleGPUCurveSample { float4 value0; float4 value1; };
struct ParticleGPUPhase {
	uint instructionOffset;
	uint instructionCount;
	uint lifeEndMode;
	uint reserved;
	float4 lifetime;
};

uint ParticleHash(uint value) {
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ (value >> 16);
}

float ParticleRandom(inout uint state) {
	state = ParticleHash(state + 0x9e3779b9u);
	return float(state >> 8) * (1.0f / 16777216.0f);
}

float3 ParticleRandom3(inout uint state) {
	float x = ParticleRandom(state);
	float y = ParticleRandom(state);
	float z = ParticleRandom(state);
	return float3(x, y, z);
}

float3 ParticleNormalize(float3 value, float3 fallback) {
	float lengthSquared = dot(value, value);
	return lengthSquared > 1e-12f ? value * rsqrt(lengthSquared) : fallback;
}

float4 ParticleQuaternionMultiply(float4 a, float4 b) {
	return float4(a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz), a.w * b.w - dot(a.xyz, b.xyz));
}

float3 ParticleRotate(float3 value, float4 rotation) {
	return value + 2.0f * cross(rotation.xyz, cross(rotation.xyz, value) + rotation.w * value);
}

float4 ParticleAxisAngle(float3 axis, float angleDegrees) {
	float halfAngle = radians(angleDegrees) * 0.5f;
	return float4(ParticleNormalize(axis, float3(1, 0, 0)) * sin(halfAngle), cos(halfAngle));
}

float4 ParticleEuler(float3 degrees) {
	float3 s = sin(radians(degrees) * 0.5f);
	float3 c = cos(radians(degrees) * 0.5f);
	return normalize(float4(s.x * c.y * c.z - c.x * s.y * s.z,
		c.x * s.y * c.z + s.x * c.y * s.z, c.x * c.y * s.z - s.x * s.y * c.z,
		c.x * c.y * c.z + s.x * s.y * s.z));
}

float4 ParticleLookRotation(float3 forward) {
	float3 z = ParticleNormalize(forward, float3(0, 0, 1));
	float3 up = abs(z.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0);
	float3 x = normalize(cross(up, z));
	float3 y = cross(z, x);
	float4 q;
	float trace = x.x + y.y + z.z;
	if (trace > 0) {
		float s = sqrt(trace + 1) * 2;
		q = float4((y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, s * 0.25f);
	} else if (x.x > y.y && x.x > z.z) {
		float s = sqrt(1 + x.x - y.y - z.z) * 2;
		q = float4(s * 0.25f, (x.y + y.x) / s, (x.z + z.x) / s, (y.z - z.y) / s);
	} else if (y.y > z.z) {
		float s = sqrt(1 + y.y - x.x - z.z) * 2;
		q = float4((x.y + y.x) / s, s * 0.25f, (y.z + z.y) / s, (z.x - x.z) / s);
	} else {
		float s = sqrt(1 + z.z - x.x - y.y) * 2;
		q = float4((x.z + z.x) / s, (y.z + z.y) / s, s * 0.25f, (x.y - y.x) / s);
	}
	return normalize(q);
}

float3 ParticleWorldPosition(ParticleAttributesGPU particle, ParticleParentGPU parent) {
	return particle.hasParent != 0 ? mul(float4(particle.positionAge.xyz, 1), parent.matrix).xyz : particle.positionAge.xyz;
}

#endif
