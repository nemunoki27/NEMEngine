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
	uint3 simulationPadding;
};

#include "particleGPUModules.hlsli"
#include "particleGPUEmitter.hlsli"

RWStructuredBuffer<ParticleAttributesGPU> gGPUParticles : register(u0);
RWStructuredBuffer<uint> gGPUStatistics : register(u1);

groupshared uint groupSpawnCount;
groupshared uint groupSpawnBase;
groupshared uint groupAliveCount;

void UpdateParent(inout ParticleAttributesGPU particle, bool transition, bool preserveRotationScale) {
	ParticleParentGPU parent = gGPUParents[particle.phase];
	if (parent.valid == 0 && particle.hasParent != 0) {
		if (parent.keepWorldOnDetach != 0) {
			ParticleParentGPU previous = gGPUParents[particle.parentPhase];
			float4x4 matrix = transition ? previous.matrix : previous.previousMatrix;
			float4 rotation = transition ? previous.rotation : previous.previousRotation;
			float3 scale = transition ? previous.scale.xyz : previous.previousScale.xyz;
			particle.positionAge.xyz = mul(float4(particle.positionAge.xyz, 1), matrix).xyz;
			particle.velocityLifetime.xyz = mul(float4(particle.velocityLifetime.xyz, 0), matrix).xyz;
			particle.rotation = normalize(ParticleQuaternionMultiply(rotation, particle.rotation));
			particle.scaleSize.xyz *= scale;
		}
		particle.hasParent = 0;
	} else if (parent.valid != 0 && particle.hasParent == 0) {
		particle.positionAge.xyz = mul(float4(particle.positionAge.xyz, 1), parent.inverse).xyz;
		particle.velocityLifetime.xyz = mul(float4(particle.velocityLifetime.xyz, 0), parent.inverse).xyz;
		if (preserveRotationScale) {
			particle.rotation = normalize(ParticleQuaternionMultiply(parent.rotation * float4(-1, -1, -1, 1), particle.rotation));
			particle.scaleSize.xyz /= parent.scale.xyz;
		}
		particle.hasParent = 1;
	}
	particle.parentPhase = particle.phase;
}

float Lifetime(ParticleAttributesGPU particle) {
	uint random = ParticleHash(seed ^ particle.birthLow ^ ParticleHash(particle.birthHigh) ^ ParticleHash(particle.phase));
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
	uint random = ParticleHash(seed ^ particle.birthLow ^ ParticleHash(particle.birthHigh));
	float3 position;
	float3 direction;
	ParticleEmitter(claim, random, position, direction);
	particle.positionAge.xyz = position;
	particle.velocityLifetime = float4(direction * lerp(speedTime.x, speedTime.y, ParticleRandom(random)), Lifetime(particle));
	ParticleRunModules(particle, 0, true, seed, index);
	particle.positionAge.xyz = mul(float4(particle.positionAge.xyz, 1), emitterWorld).xyz;
	particle.velocityLifetime.xyz = mul(float4(particle.velocityLifetime.xyz, 0), emitterWorld).xyz;
	UpdateParent(particle, true, false);
	ParticleRunModules(particle, 0, false, seed, index);
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
	bool canSpawn = index < capacity && alive == 0 && spawnCount != 0;
	GroupMemoryBarrierWithGroupSync();

	// 空き粒子の割当をGroupごとにまとめる
	uint localClaim = 0;
	if (canSpawn) InterlockedAdd(groupSpawnCount, 1u, localClaim);
	GroupMemoryBarrierWithGroupSync();
	if (localIndex == 0 && groupSpawnCount != 0) InterlockedAdd(gGPUStatistics[2], groupSpawnCount, groupSpawnBase);
	GroupMemoryBarrierWithGroupSync();
	uint claim = groupSpawnBase + localClaim;
	if (canSpawn && claim < spawnCount) {
		gGPUParticles[index] = SpawnParticle(claim, index);
		alive = 1;
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
	if (operation == 4u) UpdateParent(particle, false, true);
	else if (operation == 1u) {
		UpdateParent(particle, false, true);
		particle.positionAge.w += speedTime.z;
		if (particle.positionAge.w >= particle.velocityLifetime.w) {
			uint endMode = gGPUPhases[particle.phase].lifeEndMode;
			if (endMode == 0u && particle.phase + 1 < phaseCount) {
				++particle.phase;
				particle.positionAge.w = 0;
				particle.velocityLifetime.w = Lifetime(particle);
				UpdateParent(particle, true, true);
				ParticleRunModules(particle, 0, true, seed, index);
			} else if (endMode == 1u) particle.positionAge.w = particle.velocityLifetime.w;
			else if (endMode == 2u) particle.positionAge.w = 0;
			else particle.alive = 0;
		}
		if (particle.alive != 0) {
			particle.positionAge.xyz += particle.velocityLifetime.xyz * speedTime.z;
			ParticleRunModules(particle, speedTime.z, false, seed, index);
		}
	}
	gGPUParticles[index] = particle;
}
