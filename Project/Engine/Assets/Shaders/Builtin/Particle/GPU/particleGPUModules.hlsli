#ifndef NEM_PARTICLE_GPU_MODULES
#define NEM_PARTICLE_GPU_MODULES
#include "particleGPUShared.hlsli"

StructuredBuffer<ParticleGPUInstruction> gGPUInstructions : register(t0);
StructuredBuffer<ParticleGPUCurveSample> gGPUCurves : register(t1);
StructuredBuffer<ParticleGPUPhase> gGPUPhases : register(t2);
StructuredBuffer<ParticleParentGPU> gGPUParents : register(t3);
RWStructuredBuffer<float4> gGPUModuleStates : register(u2);

ParticleGPUCurveSample ParticleCurve(ParticleGPUInstruction instruction, float progress) {
	float position = saturate(progress) * (instruction.curveCount - 1);
	uint index = min((uint) position, instruction.curveCount - 2);
	ParticleGPUCurveSample a = gGPUCurves[instruction.curveOffset + index];
	ParticleGPUCurveSample b = gGPUCurves[instruction.curveOffset + index + 1];
	a.value0 = lerp(a.value0, b.value0, position - index);
	a.value1 = lerp(a.value1, b.value1, position - index);
	return a;
}

void ParticleRunModules(inout ParticleAttributesGPU particle, float deltaTime, bool spawn, uint seed, uint particleIndex) {
	ParticleGPUPhase phase = gGPUPhases[particle.phase];
	float progress = particle.positionAge.w / max(particle.velocityLifetime.w, 0.001f);
	for (uint i = 0; i < phase.instructionCount; ++i) {
		uint index = phase.instructionOffset + i;
		ParticleGPUInstruction instruction = gGPUInstructions[index];
		if (spawn && instruction.operation != 7u && instruction.operation != 8u) continue;
		ParticleGPUCurveSample sample = (ParticleGPUCurveSample) 0;
		if (!spawn && instruction.operation != 9u && instruction.curveCount > 1) sample = ParticleCurve(instruction, progress);
		switch (instruction.operation) {
		case 0u: particle.color = sample.value0; break;
		case 1u: particle.scaleSize.w = sample.value0.x; break;
		case 2u: particle.scaleSize.xyz = sample.value0.xyz; break;
		case 3u: particle.emissive = sample.value0; break;
		case 4u: particle.uvRotationPivotAlpha.w = sample.value0.x; break;
		case 5u:
			particle.uv.xy = instruction.flags != 0 ? particle.uv.xy + instruction.parameter0.xy * deltaTime : sample.value0.xy;
			particle.uv.zw = sample.value0.zw;
			particle.uvRotationPivotAlpha.xyz = sample.value1.xyz;
			break;
		case 6u:
			particle.velocityLifetime.xyz += instruction.parameter0.xyz * deltaTime;
			if (instruction.flags != 0 && particle.positionAge.y <= instruction.parameter0.w && particle.velocityLifetime.y < 0) {
				particle.velocityLifetime.xyz *= float3(1, -1, 1) * instruction.parameter1.x;
				particle.positionAge.y = instruction.parameter0.w;
			}
			break;
		case 7u: {
			uint random = ParticleHash(seed ^ particle.birthLow ^ ParticleHash(particle.birthHigh) ^ ParticleHash(index));
			float3 angle;
			float3 speed;
			float4 rotation;
			if ((instruction.flags & 2u) == 0) {
				angle = lerp(instruction.parameter0.xyz, instruction.parameter1.xyz, ParticleRandom3(random));
				rotation = ParticleEuler(angle);
				speed = lerp(instruction.parameter2.xyz, instruction.parameter3.xyz, ParticleRandom3(random));
			} else {
				rotation = ParticleAxisAngle(instruction.parameter0.xyz, lerp(instruction.parameter0.w, instruction.parameter1.x, ParticleRandom(random)));
				speed = ParticleNormalize(instruction.parameter2.xyz, float3(1, 0, 0)) *
					lerp(instruction.parameter2.w, instruction.parameter3.x, ParticleRandom(random));
			}
			if ((instruction.flags & 1u) != 0) {
				uint stateIndex = particleIndex * moduleStateCount + (uint) instruction.parameter3.w;
				if (spawn) gGPUModuleStates[stateIndex] = float4(speed, 0);
				else speed = gGPUModuleStates[stateIndex].xyz;
			}
			if (spawn) particle.rotation = (instruction.flags & 1u) != 0 ? normalize(ParticleQuaternionMultiply(rotation, particle.rotation)) : rotation;
			else if ((instruction.flags & 1u) != 0) {
				float magnitude = length(speed);
				if (magnitude > 0) particle.rotation = normalize(ParticleQuaternionMultiply(ParticleAxisAngle(speed / magnitude, magnitude * deltaTime), particle.rotation));
			}
			break;
		}
		case 8u:
			if (dot(particle.velocityLifetime.xyz, particle.velocityLifetime.xyz) > 1e-12f)
				particle.rotation = ParticleLookRotation(particle.velocityLifetime.xyz);
			break;
		case 9u: {
			uint2 tiles = max((uint2) instruction.parameter0.xy, 1u.xx);
			float cycles = max(instruction.parameter0.z, 0);
			float cycle = frac(saturate(progress) * cycles);
			if (progress >= 1 && cycles > 0) cycle = asfloat(0x3f7fffff);
			uint total = (uint) instruction.parameter0.w;
			uint frame = min((uint) (cycle * total), total - 1);
			uint row = frame / tiles.x;
			uint column = frame % tiles.x;
			if (instruction.flags != 0) {
				uint low = 0;
				uint high = instruction.curveCount;
				while (low < high) {
					uint middle = (low + high) / 2;
					if (frame < (uint) gGPUCurves[instruction.curveOffset + middle].value0.y) high = middle;
					else low = middle + 1;
				}
				row = low;
				tiles.x = (uint) gGPUCurves[instruction.curveOffset + row].value0.x;
				column = frame - (row == 0 ? 0u : (uint) gGPUCurves[instruction.curveOffset + row - 1].value0.y);
			}
			particle.uv = float4(float2(column, row) / tiles, 1.0f.xx / tiles);
			break;
		}
		}
	}
}

#endif
