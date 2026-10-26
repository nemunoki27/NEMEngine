#ifndef NEM_PARTICLE_GPU_COLLISION
#define NEM_PARTICLE_GPU_COLLISION
#include "particleGPUWorld.hlsli"

struct ParticleGPUCollisionHit {
	uint hit;
	float time;
	float3 position;
	float3 normal;
	float3 velocity;
	float3 colliderVelocity;
	uint2 receiverHandle;
	float2 uv;
	uint hasUV;
};

ParticleGPUCollisionHit ParticleEmptyCollision() {
	ParticleGPUCollisionHit result = (ParticleGPUCollisionHit) 0;
	result.time = 1;
	result.receiverHandle = 0xffffffffu.xx;
	return result;
}

float ParticleColliderDistance(ParticleGPUCollider collider, float3 position, out float3 normal) {
	uint type = collider.metadata.x & 0x7fffffffu;
	float3 offset = position - collider.centerRadius.xyz;
	if (type == 0u) {
		normal = collider.axisYExtent.xyz;
		return dot(offset, normal);
	}
	if (type == 1u) {
		normal = ParticleNormalize(offset, float3(0, 1, 0));
		return length(offset) - collider.centerRadius.w;
	}
	if (type == 3u) {
		float3 axis = collider.segmentEnd.xyz - collider.segmentStart.xyz;
		float weight = saturate(dot(position - collider.segmentStart.xyz, axis) / max(dot(axis, axis), 1e-12f));
		float3 radial = position - lerp(collider.segmentStart.xyz, collider.segmentEnd.xyz, weight);
		normal = ParticleNormalize(radial, collider.axisXExtent.xyz);
		return length(radial) - collider.centerRadius.w;
	}
	float3 local = float3(dot(offset, collider.axisXExtent.xyz), dot(offset, collider.axisYExtent.xyz), dot(offset, collider.axisZExtent.xyz));
	float3 halfSize = float3(collider.axisXExtent.w, collider.axisYExtent.w, collider.axisZExtent.w);
	float3 delta = abs(local) - halfSize;
	float3 outside = max(delta, 0);
	float3 localNormal;
	if (dot(outside, outside) > 1e-12f) localNormal = normalize(outside) *
		float3(local.x < 0 ? -1 : 1, local.y < 0 ? -1 : 1, local.z < 0 ? -1 : 1);
	else {
		uint axis = delta.x > delta.y ? (delta.x > delta.z ? 0u : 2u) : (delta.y > delta.z ? 1u : 2u);
		localNormal = 0;
		localNormal[axis] = local[axis] < 0 ? -1 : 1;
	}
	normal = collider.axisXExtent.xyz * localNormal.x + collider.axisYExtent.xyz * localNormal.y + collider.axisZExtent.xyz * localNormal.z;
	return length(outside) + min(max(delta.x, max(delta.y, delta.z)), 0);
}

ParticleGPUCollisionHit ParticleMakeColliderHit(ParticleGPUCollider collider, float3 start, float3 end, float radius,
	float deltaTime, float time, float3 normal, float distance) {
	ParticleGPUCollisionHit result = ParticleEmptyCollision();
	result.hit = 1;
	result.time = time;
	result.position = lerp(start, end, time) - normal * (radius + min(distance, 0));
	result.normal = normal;
	float3 angular = float3(collider.segmentStart.w, collider.segmentEnd.w, collider.velocity.w);
	float3 movingCenter = collider.centerRadius.xyz - collider.velocity.xyz * deltaTime * (1 - time);
	result.colliderVelocity = collider.velocity.xyz + cross(angular, result.position - movingCenter);
	result.receiverHandle = collider.metadata.zw;
	return result;
}

ParticleGPUCollisionHit ParticleSweepCollider(ParticleGPUCollider collider, float3 start, float3 end, float radius, float deltaTime) {
	ParticleGPUCollisionHit result = ParticleEmptyCollision();
	// 現在姿勢の形状へ相対移動を写し、移動体の通過も判定する
	float3 relativeStart = start + collider.velocity.xyz * deltaTime;
	float3 movement = end - relativeStart;
	float3 angular = float3(collider.segmentStart.w, collider.segmentEnd.w, collider.velocity.w);
	float angularSpeed = length(angular);
	float3 angularAxis = ParticleNormalize(angular, float3(0, 1, 0));
	uint type = collider.metadata.x & 0x7fffffffu;
	if (type == 1u) {
		float3 offset = relativeStart - collider.centerRadius.xyz;
		float totalRadius = collider.centerRadius.w + radius;
		float c = dot(offset, offset) - totalRadius * totalRadius;
		if (c <= 0) {
			float3 normal = ParticleNormalize(offset, float3(0, 1, 0));
			float distance = length(offset) - totalRadius;
			if (distance >= -0.0001f && dot(movement, normal) >= 0) return result;
			return ParticleMakeColliderHit(collider, start, end, radius, deltaTime, 0, normal, distance);
		}
		float a = dot(movement, movement);
		float b = dot(offset, movement);
		float discriminant = b * b - a * c;
		if (a <= 1e-12f || discriminant < 0 || b >= 0) return result;
		float time = (-b - sqrt(discriminant)) / a;
		if (time < 0 || time > 1) return result;
		return ParticleMakeColliderHit(collider, start, end, radius, deltaTime, time,
			ParticleNormalize(offset + movement * time, float3(0, 1, 0)), 0);
	}
	if (type == 0u && angularSpeed <= 1e-8f) {
		float3 normal = collider.axisYExtent.xyz;
		float a = dot(relativeStart - collider.centerRadius.xyz, normal) - radius;
		float b = dot(end - collider.centerRadius.xyz, normal) - radius;
		if (a <= 0) {
			if (a >= -0.0001f && b >= a) return result;
			return ParticleMakeColliderHit(collider, start, end, radius, deltaTime, 0, normal, a);
		}
		if (b > 0 || b >= a) return result;
		return ParticleMakeColliderHit(collider, start, end, radius, deltaTime, a / (a - b), normal, 0);
	}
	float travel = length(movement) + angularSpeed * deltaTime *
		max(length(relativeStart - collider.centerRadius.xyz), length(end - collider.centerRadius.xyz));
	float time = 0;
	float3 normal = float3(0, 1, 0);
	for (uint step = 0; step < 96; ++step) {
		float angle = angularSpeed * deltaTime * (1 - time);
		float4 rotation = float4(angularAxis * sin(angle * 0.5f), cos(angle * 0.5f));
		float3 query = collider.centerRadius.xyz + ParticleRotate(relativeStart + movement * time - collider.centerRadius.xyz, rotation);
		float distance = ParticleColliderDistance(collider, query, normal) - radius;
		normal = ParticleRotate(normal, float4(-rotation.xyz, rotation.w));
		if (distance <= 0.0001f) {
			float3 relativeMovement = movement - cross(angular, relativeStart - collider.centerRadius.xyz) * deltaTime;
			if (time == 0 && distance >= -0.0001f && dot(relativeMovement, normal) >= 0) return result;
			return ParticleMakeColliderHit(collider, start, end, radius, deltaTime, time, normal, distance);
		}
		if (travel < 1e-8f) return result;
		time += distance / travel;
		if (time > 1) return result;
	}
	return result;
}

#include "particleGPUHeightmapCollision.hlsli"
#include "particleGPUDepthCollision.hlsli"

ParticleGPUCollisionHit ParticleFindCollision(float3 start, float3 end, float radius, float deltaTime,
	uint layerMask, uint flags, float thickness, float4 plane) {
	ParticleGPUCollisionHit result = ParticleEmptyCollision();
	uint nodeIndex = 0;
	while ((flags & 2u) != 0 && nodeIndex < worldCounts.z) {
		ParticleGPUCollisionNode node = gGPUCollisionNodes[nodeIndex];
		float nearTime = 0;
		float farTime = result.hit != 0 ? result.time : 1;
		float3 movement = end - start;
		for (uint axis = 0; axis < 3; ++axis) {
			if (abs(movement[axis]) < 1e-12f) {
				if (start[axis] < node.minimum[axis] - radius || start[axis] > node.maximum[axis] + radius) farTime = -1;
			} else {
				float a = (node.minimum[axis] - radius - start[axis]) / movement[axis];
				float b = (node.maximum[axis] + radius - start[axis]) / movement[axis];
				nearTime = max(nearTime, min(a, b));
				farTime = min(farTime, max(a, b));
			}
		}
		if (nearTime > farTime) { nodeIndex = node.escape; continue; }
		++nodeIndex;
		if (node.collider == 0xffffffffu) continue;
		ParticleGPUCollider collider = gGPUColliders[node.collider];
		if ((collider.metadata.y & layerMask) == 0 || ((collider.metadata.x & 0x80000000u) != 0 && (flags & 32u) == 0)) continue;
		ParticleGPUCollisionHit candidate = ParticleSweepCollider(collider, start, end, radius, deltaTime);
		if (candidate.hit != 0 && (result.hit == 0 || candidate.time < result.time)) result = candidate;
	}
	if ((flags & 4u) != 0) {
		ParticleGPUCollider collider = (ParticleGPUCollider) 0;
		collider.centerRadius.xyz = plane.xyz * plane.w;
		collider.axisYExtent.xyz = ParticleNormalize(plane.xyz, float3(0, 1, 0));
		collider.metadata.zw = 0xffffffffu.xx;
		ParticleGPUCollisionHit candidate = ParticleSweepCollider(collider, start, end, radius, deltaTime);
		if (candidate.hit != 0 && (result.hit == 0 || candidate.time < result.time)) result = candidate;
	}
	for (uint heightmap = 0; (flags & 16u) != 0 && heightmap < worldCounts.w; ++heightmap) {
		ParticleGPUCollisionHit candidate = ParticleSweepHeightmap(gGPUHeightmaps[heightmap], start, end, radius, layerMask, deltaTime);
		if (candidate.hit != 0 && (result.hit == 0 || candidate.time < result.time)) result = candidate;
	}
	if ((flags & 8u) != 0 && depthParameters.w != 0) {
		ParticleGPUCollisionHit candidate = ParticleSweepDepth(start, end, radius, thickness);
		if (candidate.hit != 0 && (result.hit == 0 || candidate.time < result.time)) result = candidate;
	}
	return result;
}

// 移動Moduleと速度積分の後に一度だけ呼ぶ
ParticleGPUCollisionHit ParticleResolveCollisionSettings(inout ParticleAttributesGPU particle, float3 start, float deltaTime,
	float4 response, uint layerMask, uint flags, float thickness, float4 plane) {
	ParticleGPUCollisionHit first = ParticleEmptyCollision();
	if ((flags & 1u) == 0) return first;
	float remaining = deltaTime;
	float3 end = particle.positionAge.xyz;
	for (uint contact = 0; contact < 4; ++contact) {
		ParticleGPUCollisionHit hit = ParticleFindCollision(start, end, response.x, remaining, layerMask, flags, thickness, plane);
		if (hit.hit == 0) break;
		hit.velocity = particle.velocityLifetime.xyz;
		if (first.hit == 0) first = hit;
		if ((uint) response.w == 2u) { particle.alive = 0; return first; }
		float3 center = hit.position + hit.normal * (response.x + 0.0002f);
		if ((uint) response.w == 1u) {
			particle.velocityLifetime.xyz = hit.colliderVelocity;
			end = center;
			break;
		}
		float3 relative = particle.velocityLifetime.xyz - hit.colliderVelocity;
		float normalSpeed = dot(relative, hit.normal);
		float3 tangent = relative - normalSpeed * hit.normal;
		particle.velocityLifetime.xyz = hit.colliderVelocity + tangent * (1 - response.z) -
			min(normalSpeed, 0) * response.y * hit.normal + max(normalSpeed, 0) * hit.normal;
		remaining *= 1 - hit.time;
		start = center;
		end = contact == 3 ? center : center + particle.velocityLifetime.xyz * remaining;
	}
	particle.positionAge.xyz = end;
	return first;
}

ParticleGPUCollisionHit ParticleResolveCollision(inout ParticleAttributesGPU particle, float3 start, float deltaTime,
	ParticleGPUInstruction instruction) {
	return ParticleResolveCollisionSettings(particle, start, deltaTime, instruction.parameter0,
		asuint(instruction.parameter1.x), instruction.flags, instruction.parameter1.y, instruction.parameter2);
}

ParticleGPUCollisionHit ParticleResolveCollision(inout ParticleAttributesGPU particle, float3 start, float deltaTime) {
	return ParticleResolveCollisionSettings(particle, start, deltaTime, worldCollision, worldOptions.z,
		(worldOptions.x != 0 ? 1u : 0u) | 2u | 16u | (worldOptions.y != 0 ? 8u : 0u), depthParameters.z, 0);
}

#endif
