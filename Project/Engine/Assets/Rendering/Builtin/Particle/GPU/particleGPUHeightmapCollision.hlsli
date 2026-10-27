#ifndef NEM_PARTICLE_GPU_HEIGHTMAP_COLLISION
#define NEM_PARTICLE_GPU_HEIGHTMAP_COLLISION

float3 ParticleClosestTriangle(float3 samplePoint, float3 a, float3 b, float3 c) {
	float3 ab = b - a;
	float3 ac = c - a;
	float3 ap = samplePoint - a;
	float d1 = dot(ab, ap);
	float d2 = dot(ac, ap);
	if (d1 <= 0 && d2 <= 0) return a;
	float3 bp = samplePoint - b;
	float d3 = dot(ab, bp);
	float d4 = dot(ac, bp);
	if (d3 >= 0 && d4 <= d3) return b;
	float vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / max(d1 - d3, 1e-12f));
	float3 cp = samplePoint - c;
	float d5 = dot(ab, cp);
	float d6 = dot(ac, cp);
	if (d6 >= 0 && d5 <= d6) return c;
	float vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / max(d2 - d6, 1e-12f));
	float va = d3 * d6 - d5 * d4;
	if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0)
		return b + (c - b) * ((d4 - d3) / max(d4 - d3 + d5 - d6, 1e-12f));
	float denominator = max(va + vb + vc, 1e-12f);
	return a + ab * (vb / denominator) + ac * (vc / denominator);
}

float3 ParticleHeightVertex(ParticleGPUHeightmap field, uint2 cell) {
	float height = gGPUHeightSamples[field.grid.z + cell.x + field.grid.x * cell.y] * field.size.y;
	float2 xz = (float2(cell) / (field.grid.xy - 1) - 0.5f) * field.size.xz;
	return mul(float4(xz.x, height, xz.y, 1), field.localToWorld).xyz;
}

void ParticleSweepHeightTriangle(ParticleGPUHeightmap field, float3 a, float3 b, float3 c, float3 start, float3 end,
	float radius, float deltaTime, inout ParticleGPUCollisionHit result) {
	float3 relativeStart = start + field.velocity.xyz * deltaTime;
	float3 origin = field.localToWorld[3].xyz;
	float angularSpeed = length(field.angularVelocity.xyz);
	float3 angularAxis = ParticleNormalize(field.angularVelocity.xyz, float3(0, 1, 0));
	float orbit = max(length(relativeStart - origin), length(end - origin));
	float motionRadius = 2 * sin(min(angularSpeed * deltaTime, 3.14159265359f) * 0.5f) * orbit;
	float3 triangleMin = min(a, min(b, c)) - radius;
	float3 triangleMax = max(a, max(b, c)) + radius;
	if (any(max(relativeStart, end) + motionRadius < triangleMin) || any(min(relativeStart, end) - motionRadius > triangleMax)) return;
	float3 movement = end - relativeStart;
	float travel = length(movement) + angularSpeed * deltaTime * orbit;
	float time = 0;
	for (uint iteration = 0; iteration < 96; ++iteration) {
		float angle = angularSpeed * deltaTime * (1 - time);
		float4 rotation = float4(angularAxis * sin(angle * 0.5f), cos(angle * 0.5f));
		float3 center = origin + ParticleRotate(relativeStart + movement * time - origin, rotation);
		float3 samplePoint = ParticleClosestTriangle(center, a, b, c);
		float3 difference = center - samplePoint;
		float distance = length(difference) - radius;
		if (distance <= 0.0001f) {
			float3 normal = ParticleNormalize(difference, ParticleNormalize(cross(c - a, b - a), float3(0, 1, 0)));
			normal = ParticleRotate(normal, float4(-rotation.xyz, rotation.w));
			float3 relativeMovement = movement - cross(field.angularVelocity.xyz, relativeStart - origin) * deltaTime;
			if (time == 0 && distance >= -0.0001f && dot(relativeMovement, normal) >= 0) return;
			if (result.hit != 0 && time >= result.time) return;
			result.hit = 1;
			result.time = time;
			float3 movingOrigin = origin - field.velocity.xyz * deltaTime * (1 - time);
			result.position = movingOrigin + ParticleRotate(samplePoint - origin, float4(-rotation.xyz, rotation.w));
			result.normal = normal;
			result.colliderVelocity = field.velocity.xyz + cross(field.angularVelocity.xyz, result.position - movingOrigin);
			float3 local = mul(float4(samplePoint, 1), field.worldToLocal).xyz;
			result.uv = local.xz / field.size.xz + 0.5f;
			return;
		}
		if (travel <= 1e-8f) return;
		time += distance / travel;
		if (time > (result.hit != 0 ? result.time : 1)) return;
	}
}

ParticleGPUCollisionHit ParticleSweepHeightmap(ParticleGPUHeightmap field, float3 start, float3 end, float radius,
	uint layerMask, float deltaTime) {
	ParticleGPUCollisionHit result = ParticleEmptyCollision();
	if ((field.grid.w & layerMask) == 0) return result;
	float3 relativeStart = start + field.velocity.xyz * deltaTime;
	float3 localStart = mul(float4(relativeStart, 1), field.worldToLocal).xyz;
	float3 localEnd = mul(float4(end, 1), field.worldToLocal).xyz;
	float2 cellSize = field.size.xz / (field.grid.xy - 1);
	float2 gridStart = (localStart.xz + field.size.xz * 0.5f) / cellSize;
	float2 gridEnd = (localEnd.xz + field.size.xz * 0.5f) / cellSize;
	float2 movement = gridEnd - gridStart;
	float orbit = max(length(relativeStart - field.localToWorld[3].xyz), length(end - field.localToWorld[3].xyz));
	float motionRadius = 2 * sin(min(length(field.angularVelocity.xyz) * deltaTime, 3.14159265359f) * 0.5f) * orbit;
	float2 localRadius = (radius + motionRadius) * float2(length(float3(field.worldToLocal[0][0], field.worldToLocal[1][0], field.worldToLocal[2][0])),
		length(float3(field.worldToLocal[0][2], field.worldToLocal[1][2], field.worldToLocal[2][2])));
	float2 neighborCount = ceil(localRadius / cellSize);
	int2 neighbors = (int2) min(neighborCount, 8192.0f.xx);
	if (any(neighborCount > 8192)) {
		gridStart = gridEnd = float2(field.grid.xy - 1) * 0.5f;
		movement = 0;
		neighbors = (int2) field.grid.xy - 1;
	}
	float enter = 0;
	float leave = 1;
	for (uint axis = 0; axis < 2; ++axis) {
		float low = -float(neighbors[axis]);
		float high = float(field.grid[axis] - 1 + neighbors[axis]);
		if (abs(movement[axis]) < 1e-8f) { if (gridStart[axis] < low || gridStart[axis] > high) return result; }
		else {
			float2 interval = (float2(low, high) - gridStart[axis]) / movement[axis];
			enter = max(enter, min(interval.x, interval.y));
			leave = min(leave, max(interval.x, interval.y));
		}
	}
	if (enter > leave) return result;
	float2 first = gridStart + movement * enter;
	int2 cell = (int2) floor(first);
	int2 direction = int2(movement.x >= 0 ? 1 : -1, movement.y >= 0 ? 1 : -1);
	float2 next = float2(abs(movement.x) > 1e-8f ? (cell.x + (direction.x > 0 ? 1 : 0) - gridStart.x) / movement.x : 1e30f,
		abs(movement.y) > 1e-8f ? (cell.y + (direction.y > 0 ? 1 : 0) - gridStart.y) / movement.y : 1e30f);
	float2 stride = 1 / max(abs(movement), 1e-30f);
	uint maxSteps = field.grid.x + field.grid.y + 2u + 2u * (uint) (neighbors.x + neighbors.y);
	for (uint step = 0; step < maxSteps; ++step) {
		int2 lowCell = max(cell - neighbors, 0);
		int2 highCell = min(cell + neighbors, (int2) field.grid.xy - 2);
		for (int y = lowCell.y; y <= highCell.y; ++y) {
			for (int x = lowCell.x; x <= highCell.x; ++x) {
				uint2 coordinate = uint2(x, y);
				float3 a = ParticleHeightVertex(field, coordinate);
				float3 b = ParticleHeightVertex(field, coordinate + uint2(1, 0));
				float3 c = ParticleHeightVertex(field, coordinate + uint2(0, 1));
				float3 d = ParticleHeightVertex(field, coordinate + uint2(1, 1));
				ParticleSweepHeightTriangle(field, a, b, c, start, end, radius, deltaTime, result);
				ParticleSweepHeightTriangle(field, b, d, c, start, end, radius, deltaTime, result);
			}
		}
		uint advance = next.x < next.y ? 0u : 1u;
		if (next[advance] > leave) break;
		cell[advance] += direction[advance];
		next[advance] += stride[advance];
	}
	if (result.hit != 0) {
		result.receiverHandle = field.receiver.xy;
		result.hasUV = 1;
	}
	return result;
}

ParticleGPUCollisionHit ParticleSweepHeightmap(ParticleGPUHeightmap field, float3 start, float3 end, float radius, uint layerMask) {
	return ParticleSweepHeightmap(field, start, end, radius, layerMask, 0);
}

#endif
