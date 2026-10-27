#include "particleGPUShared.hlsli"

cbuffer ParticleGPUSimulationConstants : register(b0) {
	float4x4 emitterWorld;
	float4 offsetMinimum;
	float4 offsetMaximum;
	float4 speedTime;
	float4 shapeParameters;
	float4 shapeSize;
	float4 pointDirection;
	float4 circle;
	uint capacity;
	uint phaseCount;
	uint spawnCount;
	uint seed;
	uint birthLow;
	uint birthHigh;
	uint shape;
	uint operation;
	uint faceMask;
	uint spawnMode;
	uint velocityMode;
	uint preserveWrapDirection;
	uint moduleStateCount;
	uint spawnDirectionSlot;
	uint shapeStateSlot;
	uint initialShapeOffset;
};

#include "particleGPUModules.hlsli"
#include "particleGPUEmitter.hlsli"
#ifdef NEM_PARTICLE_EXTENDED
#include "particleGPUCollision.hlsli"
#include "particleGPUEvents.hlsli"
#include "particleGPUSource.hlsli"

ParticleGPUImpact ParticleEventImpact(ParticleAttributesGPU particle) {
	ParticleGPUImpact impact = (ParticleGPUImpact) 0;
	ParticleAttributesGPU world = ParticleToWorld(particle);
	impact.position = world.positionAge.xyz;
	impact.velocity = world.velocityLifetime.xyz;
	impact.normal = ParticleNormalize(-world.velocityLifetime.xyz, float3(0, 1, 0));
	impact.receiver = 0xffffffffu.xx;
	return impact;
}
#endif

RWStructuredBuffer<ParticleAttributesGPU> gGPUParticles : register(u0);
RWStructuredBuffer<uint> gGPUStatistics : register(u1);

groupshared uint groupSpawnCount;
groupshared uint groupSpawnBase;
groupshared uint groupAliveCount;

void UpdateParent(inout ParticleAttributesGPU particle, bool transition, bool preserveRotationScale, uint particleIndex) {
	ParticleParentGPU parent = gGPUParents[particle.phase];
	if (parent.valid == 0 && particle.hasParent != 0) {
		if (parent.keepWorldOnDetach != 0) {
			ParticleParentGPU previous = gGPUParents[particle.parentPhase];
			float4x4 matrix = transition ? previous.matrix : previous.previousMatrix;
			float4 rotation = transition ? previous.rotation : previous.previousRotation;
			float3 scale = transition ? previous.scale.xyz : previous.previousScale.xyz;
			particle.positionAge.xyz = mul(float4(particle.positionAge.xyz, 1), matrix).xyz;
			particle.velocityLifetime.xyz = mul(float4(particle.velocityLifetime.xyz, 0), matrix).xyz;
			if (spawnDirectionSlot != 0xffffffffu) {
				uint slot = particleIndex * moduleStateCount + spawnDirectionSlot;
				gGPUModuleStates[slot].xyz = ParticleNormalize(mul(float4(gGPUModuleStates[slot].xyz, 0), matrix).xyz, float3(0, 1, 0));
			}
			particle.rotation = normalize(ParticleQuaternionMultiply(rotation, particle.rotation));
			particle.scaleSize.xyz *= scale;
		}
		particle.hasParent = 0;
	} else if (parent.valid != 0 && particle.hasParent == 0) {
		particle.positionAge.xyz = mul(float4(particle.positionAge.xyz, 1), parent.inverse).xyz;
		particle.velocityLifetime.xyz = mul(float4(particle.velocityLifetime.xyz, 0), parent.inverse).xyz;
		if (spawnDirectionSlot != 0xffffffffu) {
			uint slot = particleIndex * moduleStateCount + spawnDirectionSlot;
			gGPUModuleStates[slot].xyz = ParticleNormalize(mul(float4(gGPUModuleStates[slot].xyz, 0), parent.inverse).xyz, float3(0, 1, 0));
		}
		if (preserveRotationScale) {
			particle.rotation = normalize(ParticleQuaternionMultiply(parent.rotation * float4(-1, -1, -1, 1), particle.rotation));
			particle.scaleSize.xyz /= parent.scale.xyz;
		}
		particle.hasParent = 1;
	}
	particle.parentPhase = particle.phase;
}

float Lifetime(ParticleAttributesGPU particle, uint particleIndex) {
	uint particleSeed = seed;
#ifdef NEM_PARTICLE_EXTENDED
	particleSeed = ParticleGPUSeed(particleIndex, seed);
#endif
	uint random = ParticleHash(particleSeed ^ particle.birthLow ^ ParticleHash(particle.birthHigh) ^ ParticleHash(particle.phase));
	return max(lerp(gGPUPhases[particle.phase].lifetime.x, gGPUPhases[particle.phase].lifetime.y, ParticleRandom(random)), 0.001f);
}

ParticleAttributesGPU SpawnParticle(uint claim, uint index) {
	ParticleAttributesGPU particle = (ParticleAttributesGPU) 0;
	particle.alive = 1;
	particle.birthLow = birthLow + claim;
	particle.birthHigh = birthHigh + (particle.birthLow < birthLow ? 1u : 0u);
	particle.rotation.w = 1;
	particle.color = 1;
	particle.scaleSize = 1;
	particle.uv.zw = 1;
	particle.trailIndex = 0xffffffffu;
	uint particleSeed = seed;
#ifdef NEM_PARTICLE_EXTENDED
	bool isChild = claim >= spawnCount;
	ParticleGPUEvent child = (ParticleGPUEvent) 0;
	if (isChild) child = gGPUChildren[claim - spawnCount];
	ParticleGPUPrepareInheritedState(index, isChild, child);
	particleSeed = ParticleGPUSeed(index, seed);
#endif
	uint random = ParticleHash(particleSeed ^ particle.birthLow ^ ParticleHash(particle.birthHigh));
#ifdef NEM_PARTICLE_EXTENDED
	if (isChild && (child.flags & 16u) != 0u) random = child.seed;
#endif
	float3 position;
	float3 direction;
	ParticleEmitter(claim, random, position, direction);
	particle.positionAge.xyz = position;
	particle.velocityLifetime = float4(direction * lerp(speedTime.x, speedTime.y, ParticleRandom(random)), Lifetime(particle, index));
	// 初期Phaseだけ基本形状を設定し、以降は前Phaseの形状を残す
	if (shapeStateSlot != 0xffffffffu) {
		uint slot = index * moduleStateCount + shapeStateSlot;
		for (uint part = 0; part < 3; ++part) {
			ParticleGPUCurveSample initial = gGPUCurves[initialShapeOffset + part];
			gGPUModuleStates[slot + part * 2] = initial.value0;
			if (part < 2) gGPUModuleStates[slot + part * 2 + 1] = initial.value1;
		}
	}
	if (spawnDirectionSlot != 0xffffffffu) gGPUModuleStates[index * moduleStateCount + spawnDirectionSlot] = float4(direction, 0);
	ParticleRunModules(particle, 0, true, particleSeed, index, 0);
	particle.positionAge.xyz = mul(float4(particle.positionAge.xyz, 1), emitterWorld).xyz;
	particle.velocityLifetime.xyz = mul(float4(particle.velocityLifetime.xyz, 0), emitterWorld).xyz;
	if (spawnDirectionSlot != 0xffffffffu) {
		uint slot = index * moduleStateCount + spawnDirectionSlot;
		gGPUModuleStates[slot].xyz = ParticleNormalize(mul(float4(direction, 0), emitterWorld).xyz, float3(0, 1, 0));
	}

#ifdef NEM_PARTICLE_EXTENDED
	if (!isChild && !ParticleSourceSpawn(particle, claim, index, random)) return particle;
	if (isChild) {
		ParticleGPUApplyChild(particle, child);
		if (sourceKind != 0u) {
			uint slot = index * sourceModuleStateCount + sourceStateSlot;
			gGPUModuleStates[slot] = float4(asfloat(0xffffffffu), 0, 0, 0);
			gGPUModuleStates[slot + 1u] = 1;
		}
	}
	if (spawnDirectionSlot != 0xffffffffu) {
		uint slot = index * moduleStateCount + spawnDirectionSlot;
		gGPUModuleStates[slot].xyz = ParticleNormalize(particle.velocityLifetime.xyz, float3(0, 1, 0));
	}
#endif
	UpdateParent(particle, true, false, index);
#ifdef NEM_PARTICLE_EXTENDED
	ParticleInitializeSpline(particle, index);
#endif
	ParticleRunModules(particle, 0, false, particleSeed, index, 0);
#ifdef NEM_PARTICLE_EXTENDED
	ParticleSourceApplyColor(particle, index);
	ParticleGPUApplyInheritedColor(particle, index);
	ParticleGPUEmitEvent(2u, index, particle, ParticleEventImpact(particle), -1);
#endif
	return particle;
}

void SpawnParticles(uint index, uint localIndex) {
	if (localIndex == 0) {
		groupSpawnCount = 0;
		groupSpawnBase = 0;
		groupAliveCount = 0;
	}
	uint alive = 0;
	if (index < capacity) alive = gGPUParticles[index].alive;
	uint totalSpawn = spawnCount;
#ifdef NEM_PARTICLE_EXTENDED
	totalSpawn += ParticleGPUChildCount();
#endif
	bool canSpawn = index < capacity && alive == 0 && totalSpawn != 0;
	GroupMemoryBarrierWithGroupSync();

	// 空き粒子の割当をGroupごとにまとめる
	uint localClaim = 0;
	if (canSpawn) InterlockedAdd(groupSpawnCount, 1u, localClaim);
	GroupMemoryBarrierWithGroupSync();
	if (localIndex == 0 && groupSpawnCount != 0) InterlockedAdd(gGPUStatistics[2], groupSpawnCount, groupSpawnBase);
	GroupMemoryBarrierWithGroupSync();
	uint claim = groupSpawnBase + localClaim;
	if (canSpawn && claim < totalSpawn) {
		gGPUParticles[index] = SpawnParticle(claim, index);
		alive = gGPUParticles[index].alive;
	}

	// 生存数は共有メモリで集計してから一度だけ加算する
	if (index < capacity && alive != 0) InterlockedAdd(groupAliveCount, 1u);
	GroupMemoryBarrierWithGroupSync();
	if (localIndex == 0 && groupAliveCount != 0) InterlockedAdd(gGPUStatistics[0], groupAliveCount);
}

[numthreads(256, 1, 1)]
void main(uint index : SV_DispatchThreadID, uint localIndex : SV_GroupIndex) {
	if (operation == 3u || operation == 5u) {
		if (index < (operation == 5u ? 4u : 3u)) gGPUStatistics[index] = 0;
		return;
	}
	if (operation == 2u) {
		// 範囲外の末尾スレッドもGroupの同期には参加させる
		SpawnParticles(index, localIndex);
		return;
	}
	if (index >= capacity) return;
	if (operation == 0u) {
		ParticleAttributesGPU particle = (ParticleAttributesGPU) 0;
		particle.trailIndex = 0xffffffffu;
		gGPUParticles[index] = particle;
		return;
	}
	// 未使用の属性は読み書きせず、生存粒子だけを更新する
	if (gGPUParticles[index].alive == 0) return;
	ParticleAttributesGPU particle = gGPUParticles[index];
	if (operation == 4u) UpdateParent(particle, false, true, index);
	else if (operation == 1u) {
		UpdateParent(particle, false, true, index);
		float previousAge = particle.positionAge.w;
		uint particleSeed = seed;
#ifdef NEM_PARTICLE_EXTENDED
		particleSeed = ParticleGPUSeed(index, seed);
		bool phaseEntered = false;
		bool timeEmitted = false;
		ParticleSourceAttach(particle, index);
		float3 previousPosition = ParticleToWorld(particle).positionAge.xyz;
#endif
		particle.positionAge.w += speedTime.z;
		if (particle.positionAge.w >= particle.velocityLifetime.w) {
#ifdef NEM_PARTICLE_EXTENDED
			ParticleGPUEmitEvent(2u, index, particle, ParticleEventImpact(particle), previousAge);
			timeEmitted = true;
#endif
			uint endMode = gGPUPhases[particle.phase].lifeEndMode;
			if (endMode == 0u && particle.phase + 1 < phaseCount) {
#ifdef NEM_PARTICLE_EXTENDED
				ParticleGPUEmitEvent(3u, index, particle, ParticleEventImpact(particle), previousAge);
				phaseEntered = true;
#endif
				++particle.phase;
				previousAge = 0;
				particle.positionAge.w = 0;
				particle.velocityLifetime.w = Lifetime(particle, index);
				UpdateParent(particle, true, true, index);
				ParticleRunModules(particle, 0, true, particleSeed, index, 0);
#ifdef NEM_PARTICLE_EXTENDED
				ParticleInitializeSpline(particle, index);
#endif
			} else if (endMode == 1u) particle.positionAge.w = particle.velocityLifetime.w;
			else if (endMode == 2u) {
				particle.positionAge.w = 0;
#ifdef NEM_PARTICLE_EXTENDED
				phaseEntered = true;
				ParticleInitializeSpline(particle, index);
#endif
			}
			else {
#ifdef NEM_PARTICLE_EXTENDED
				ParticleGPUEmitEvent(1u, index, particle, ParticleEventImpact(particle), previousAge);
#endif
				particle.alive = 0;
			}
		}
		if (particle.alive != 0) {
			particle.positionAge.xyz += particle.velocityLifetime.xyz * speedTime.z;
			ParticleRunModules(particle, speedTime.z, false, particleSeed, index, previousAge);
#ifdef NEM_PARTICLE_EXTENDED
			ParticleSourceApplyColor(particle, index);
			ParticleGPUApplyInheritedColor(particle, index);
			if (phaseEntered || !timeEmitted) {
				ParticleGPUEmitEvent(2u, index, particle, ParticleEventImpact(particle), phaseEntered ? -1 : previousAge);
			}
			ParticleAttributesGPU world = ParticleToWorld(particle);
			ParticleGPUPhase phase = gGPUPhases[particle.phase];
			for (uint i = 0; i < phase.instructionCount && world.alive != 0; ++i) {
				ParticleGPUInstruction instruction = gGPUInstructions[phase.instructionOffset + i];
				if (instruction.operation != 22u) continue;
				ParticleGPUCollisionHit hit = ParticleResolveCollision(world, previousPosition, speedTime.z, instruction);
				if (hit.hit == 0) continue;
				ParticleGPUImpact impact;
				impact.position = hit.position;
				impact.normal = hit.normal;
				impact.velocity = hit.velocity;
				impact.receiver = hit.receiverHandle;
				impact.uv = hit.uv;
				impact.hasUV = hit.hasUV;
				ParticleGPUEmitEvent(0u, index, world, impact, previousAge);
				if (world.alive == 0) ParticleGPUEmitEvent(1u, index, world, impact, previousAge);
			}
			ParticleFromWorld(world);
			particle = world;
#endif
		}
	}
	gGPUParticles[index] = particle;
}
