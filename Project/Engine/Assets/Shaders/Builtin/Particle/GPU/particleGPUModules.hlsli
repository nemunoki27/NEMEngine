#ifndef NEM_PARTICLE_GPU_MODULES
#define NEM_PARTICLE_GPU_MODULES
#include "particleGPUShared.hlsli"
#include "particleGPUNoise.hlsli"
#include "particleGPUMovement.hlsli"

StructuredBuffer<ParticleGPUInstruction> gGPUInstructions : register(t0);
StructuredBuffer<ParticleGPUCurveSample> gGPUCurves : register(t1);
StructuredBuffer<ParticleGPUPhase> gGPUPhases : register(t2);
StructuredBuffer<ParticleParentGPU> gGPUParents : register(t3);
RWStructuredBuffer<float4> gGPUModuleStates : register(u2);


#ifdef NEM_PARTICLE_EXTENDED
#include "particleGPUWorld.hlsli"

ParticleAttributesGPU ParticleToWorld(ParticleAttributesGPU particle) {
	if (particle.hasParent != 0) {
		ParticleParentGPU parent = gGPUParents[particle.parentPhase];
		particle.positionAge.xyz = mul(float4(particle.positionAge.xyz, 1), parent.matrix).xyz;
		particle.velocityLifetime.xyz = mul(float4(particle.velocityLifetime.xyz, 0), parent.matrix).xyz;
		particle.rotation = ParticleQuaternionMultiply(parent.rotation, particle.rotation);
	}
	return particle;
}

void ParticleFromWorld(inout ParticleAttributesGPU particle) {
	if (particle.hasParent != 0) {
		ParticleParentGPU parent = gGPUParents[particle.parentPhase];
		particle.positionAge.xyz = mul(float4(particle.positionAge.xyz, 1), parent.inverse).xyz;
		particle.velocityLifetime.xyz = mul(float4(particle.velocityLifetime.xyz, 0), parent.inverse).xyz;
		particle.rotation = ParticleQuaternionMultiply(parent.rotation * float4(-1, -1, -1, 1), particle.rotation);
	}
}

void ParticleInitializeSpline(inout ParticleAttributesGPU particle, uint particleIndex) {
	ParticleGPUPhase phase = gGPUPhases[particle.phase];
	ParticleAttributesGPU world = ParticleToWorld(particle);
	for (uint i = 0; i < phase.instructionCount; ++i) {
		ParticleGPUInstruction instruction = gGPUInstructions[phase.instructionOffset + i];
		if (instruction.operation != 18u) continue;
		uint slot = particleIndex * moduleStateCount + (uint) instruction.parameter3.y;
		float4 state = 0;
		ParticleRunSpline(world, instruction, 0, true, state);
		gGPUModuleStates[slot] = state;
	}
	ParticleFromWorld(world);
	particle = world;
}
#endif

ParticleGPUCurveSample ParticleCurve(ParticleGPUInstruction instruction, float progress) {
	return SampleParticleGPUCurve(gGPUCurves, instruction.curveOffset, instruction.curveCount, progress);
}

void ParticleRunModules(inout ParticleAttributesGPU particle, float deltaTime, bool spawn, uint seed, uint particleIndex,
	float previousAge) {
	ParticleGPUPhase phase = gGPUPhases[particle.phase];
	float progress = particle.positionAge.w / max(particle.velocityLifetime.w, 0.001f);
	bool useParticleColorForEmissive = false;
	for (uint i = 0; i < phase.instructionCount; ++i) {
		uint index = phase.instructionOffset + i;
		ParticleGPUInstruction instruction = gGPUInstructions[index];
		if (spawn && instruction.operation != 7u && instruction.operation != 8u &&
			instruction.operation != 12u && instruction.operation != 13u && instruction.operation != 14u) continue;
		ParticleGPUCurveSample sample = (ParticleGPUCurveSample) 0;
		if (instruction.operation != 9u && instruction.curveCount > 1) sample = ParticleCurve(instruction, progress);
		switch (instruction.operation) {

#ifdef NEM_PARTICLE_EXTENDED
		case 16u:
		case 17u:
		case 18u: {
			ParticleAttributesGPU world = ParticleToWorld(particle);
			if (instruction.operation == 16u) ParticleRunForceFields(world, instruction, deltaTime);
			else if (instruction.operation == 17u) ParticleRunVectorFields(world, instruction, deltaTime);
			else {
				uint slot = particleIndex * moduleStateCount + (uint) instruction.parameter3.y;
				float4 state = gGPUModuleStates[slot];
				ParticleRunSpline(world, instruction, deltaTime, false, state);
				gGPUModuleStates[slot] = state;
			}
			ParticleFromWorld(world);
			particle = world;
			break;
		}
#endif

		case 0u: particle.color = sample.value0; break;
		case 1u: particle.scaleSize.w = sample.value0.x; break;
		case 2u: particle.scaleSize.xyz = sample.value0.xyz; break;
		case 3u:
			particle.emissive = sample.value0;
			useParticleColorForEmissive = (instruction.flags & 1u) != 0;
			break;
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
			// 補間回転と寿命角速度はCPUで評価したテーブルを参照する
			if ((instruction.flags & 4u) != 0) {
				particle.rotation = normalize(sample.value0);
				break;
			}
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
			if ((instruction.flags & 9u) == 1u) {
				uint stateIndex = particleIndex * moduleStateCount + (uint) instruction.parameter3.w;
				if (spawn) gGPUModuleStates[stateIndex] = float4(speed, 0);
				else speed = gGPUModuleStates[stateIndex].xyz;
			}
			if ((instruction.flags & 8u) != 0) speed = sample.value0.xyz;
			if (spawn) particle.rotation = (instruction.flags & 1u) != 0 ? normalize(ParticleQuaternionMultiply(rotation, particle.rotation)) : rotation;
			else if ((instruction.flags & 1u) != 0) {
				float magnitude = length(speed);
				if (magnitude > 0) particle.rotation = normalize(ParticleQuaternionMultiply(ParticleAxisAngle(speed / magnitude, magnitude * deltaTime), particle.rotation));
			}
			break;
		}
#if defined(NEM_PARTICLE_CURL_NOISE)
		case 15u:
			particle.velocityLifetime.xyz += ParticleCurlNoise(particle.positionAge.xyz + instruction.parameter1.xyz * particle.positionAge.w,
				instruction.parameter0.y) * (instruction.parameter0.x * deltaTime);
			break;
#endif
		case 10u:
			particle.velocityLifetime.xyz += ParticleNoiseVector(particle.positionAge.xyz + particle.positionAge.www * 0.5f,
				instruction.parameter0.y) * (instruction.parameter0.x * deltaTime);
			break;
		case 14u: {
			uint slot = particleIndex * moduleStateCount + shapeStateSlot + instruction.flags * 2;
			gGPUModuleStates[slot] = sample.value0;
			if (instruction.flags < 2) gGPUModuleStates[slot + 1] = sample.value1;
			break;
		}
		case 11u:
			particle.uv.xy += ParticleNoiseVector(float3(particle.positionAge.w, float(particle.birthLow) * 0.37f, 0),
				instruction.parameter0.y).xy * (instruction.parameter0.x * deltaTime);
			break;
		case 12u:
		case 13u: {
			float3 direction = gGPUModuleStates[particleIndex * moduleStateCount + spawnDirectionSlot].xyz;
			float3 offset = ParticleMovementOffset(instruction, sample, direction, particle.birthLow);
			if (!spawn) {
				ParticleGPUCurveSample previous = ParticleCurve(instruction, previousAge / max(particle.velocityLifetime.w, 0.001f));
				offset -= ParticleMovementOffset(instruction, previous, direction, particle.birthLow);
			}
			particle.positionAge.xyz += offset;
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
	// Moduleの並び順にかかわらず、確定した現在色を使う
	if (useParticleColorForEmissive) particle.emissive.xyz = particle.color.rgb;
}

#endif
