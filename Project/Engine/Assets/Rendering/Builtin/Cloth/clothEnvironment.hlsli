#ifndef NEM_CLOTH_ENVIRONMENT
#define NEM_CLOTH_ENVIRONMENT

struct ClothGPUWindField {
	float4x4 inverse;
	float4x4 world;
	float3 center;
	uint mask;
	float3 axis;
	float strength;
	float falloff;
	float tightness;
	uint shape;
	uint kind;
	uint4 grid;
};

struct ClothGPUCollider {
	float3 center;
	float radius;
	float3 axes[3];
	float3 halfExtents;
	float3 start;
	uint type;
	float3 end;
	uint mask;
};

StructuredBuffer<ClothGPUWindField> gClothFields : register(t0, space1);
StructuredBuffer<float3> gClothSamples : register(t1, space1);
StructuredBuffer<ClothGPUCollider> gClothColliders : register(t2, space1);

float3 ClothNormalizeOr(float3 v, float3 fallback) {
	float lengthSq = dot(v, v);
	return lengthSq > 1e-8f ? v * rsqrt(lengthSq) : fallback;
}

float3 ClothReadCell(uint4 grid, uint3 cell) {
	return gClothSamples[grid.w + cell.x + grid.x * (cell.y + grid.y * cell.z)];
}

float3 ClothSampleGrid(uint4 grid, float3 uvw) {
	float3 cell = saturate(uvw) * (grid.xyz - 1);
	uint3 lo = (uint3)cell;
	uint3 hi = min(lo + 1, grid.xyz - 1);
	float3 w = frac(cell);
	float3 a = lerp(ClothReadCell(grid, lo), ClothReadCell(grid, uint3(hi.x, lo.yz)), w.x);
	float3 b = lerp(ClothReadCell(grid, uint3(lo.x, hi.y, lo.z)), ClothReadCell(grid, uint3(hi.xy, lo.z)), w.x);
	float3 c = lerp(ClothReadCell(grid, uint3(lo.xy, hi.z)), ClothReadCell(grid, uint3(hi.x, lo.y, hi.z)), w.x);
	float3 d = lerp(ClothReadCell(grid, uint3(lo.x, hi.yz)), ClothReadCell(grid, hi), w.x);
	return lerp(lerp(a, b, w.y), lerp(c, d, w.y), w.z);
}

float3 ClothFieldAcceleration(float3 position, float3 velocity, uint windMask, uint fieldCount, float dt) {
	float3 acceleration = 0;
	if (!windMask) return acceleration;
	for (uint i = 0; i < fieldCount; ++i) {
		ClothGPUWindField field = gClothFields[i];
		if (!(field.mask & windMask)) continue;
		float3 local = mul(float4(position, 1), field.inverse).xyz;
		float distance = field.shape == 0 ? length(local) : max(abs(local.x), max(abs(local.y), abs(local.z)));
		if (distance > 1) continue;
		if (field.kind < 4) {
			if (distance >= 1) continue;
			float3 radial = position - field.center;
			float3 dir = field.axis;
			if (field.kind == 0) dir = ClothNormalizeOr(-radial, 0);
			else if (field.kind == 1) dir = ClothNormalizeOr(radial, 0);
			else if (field.kind == 2) dir = ClothNormalizeOr(cross(field.axis, radial), 0);
			acceleration += dir * (field.strength * pow(1 - distance, max(field.falloff, 0)));
		} else {
			float3 v = float3(0, 0, 1);
			if (field.kind == 5) v = ClothNormalizeOr(cross(float3(0, 1, 0), local), 0);
			else if (field.kind == 6) v = ClothSampleGrid(field.grid, (local + 1) * 0.5f);
			float3 target = ClothNormalizeOr(mul(float4(v, 0), field.world).xyz, 0) * (length(v) * field.strength);
			float follow = 1 - pow(1 - field.tightness, dt * 60);
			acceleration += lerp(target, (target - velocity) / dt, follow);
		}
	}
	return acceleration;
}

float3 ClothProjectRound(ClothGPUCollider collider, float3 p, float thickness) {
	float3 center = collider.center;
	float3 segment = collider.end - collider.start;
	if (collider.type == 6) {
		float lengthSq = dot(segment, segment);
		float t = lengthSq > 1e-8f ? saturate(dot(p - collider.start, segment) / lengthSq) : 0;
		center = collider.start + t * segment;
	}
	float3 delta = p - center;
	float distance = length(delta);
	float penetration = collider.radius + thickness - distance;
	if (penetration <= 0) return p;
	float3 normal = float3(1, 0, 0);
	if (distance > 1e-4f) normal = delta / distance;
	else if (collider.type == 6) {
		float3 axis = ClothNormalizeOr(segment, float3(0, 1, 0));
		float3 basis = abs(axis.z) < 0.9f ? float3(0, 0, 1) : float3(0, 1, 0);
		normal = ClothNormalizeOr(cross(axis, basis), normal);
	}
	return p + normal * penetration;
}

float3 ClothProjectBox(ClothGPUCollider box, float3 p, float thickness) {
	float3 relative = p - box.center;
	float3 local = float3(dot(relative, box.axes[0]), dot(relative, box.axes[1]), dot(relative, box.axes[2]));
	float3 clamped = clamp(local, -box.halfExtents, box.halfExtents);
	float3 closest = box.center + clamped.x * box.axes[0] + clamped.y * box.axes[1] + clamped.z * box.axes[2];
	float3 delta = p - closest;
	float distance = length(delta);
	if (distance > thickness) return p;
	if (distance > 1e-4f) return p + delta * ((thickness - distance) / distance);
	float3 remain = box.halfExtents - abs(local);
	uint axis = remain.y < remain.x ? 1 : 0;
	if (remain.z < remain[axis]) axis = 2;
	return p + box.axes[axis] * ((local[axis] < 0 ? -1 : 1) * max(0, thickness + remain[axis]));
}

float3 ClothProjectColliders(float3 position, float thickness, uint collisionMask, uint colliderCount) {
	if (!collisionMask) return position;
	for (uint i = 0; i < colliderCount; ++i) {
		ClothGPUCollider collider = gClothColliders[i];
		if (!(collisionMask & collider.mask)) continue;
		if (collider.type == 3 || collider.type == 4) position = ClothProjectBox(collider, position, thickness);
		else position = ClothProjectRound(collider, position, thickness);
	}
	return position;
}
#endif
