#ifndef NEM_PARTICLE_GPU_DEPTH_COLLISION
#define NEM_PARTICLE_GPU_DEPTH_COLLISION

float3 ParticleDepthWorld(int2 pixel, float depth) {
	float2 uv = (float2(pixel) + 0.5f) / depthParameters.xy;
	float4 world = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1), depthInverseViewProjection);
	return world.xyz / world.w;
}

ParticleGPUCollisionHit ParticleSweepDepth(float3 start, float3 end, float radius, float thickness) {
	ParticleGPUCollisionHit result = ParticleEmptyCollision();
	float4 clipStart = mul(float4(start, 1), depthViewProjection);
	float4 clipEnd = mul(float4(end, 1), depthViewProjection);
	if (clipStart.w <= 1e-6f || clipEnd.w <= 1e-6f) return result;
	float2 uvStart = float2(clipStart.x / clipStart.w * 0.5f + 0.5f, 0.5f - clipStart.y / clipStart.w * 0.5f);
	float2 uvEnd = float2(clipEnd.x / clipEnd.w * 0.5f + 0.5f, 0.5f - clipEnd.y / clipEnd.w * 0.5f);
	float2 deltaUV = uvEnd - uvStart;
	float low = 0;
	float high = 1;
	for (uint axis = 0; axis < 2; ++axis) {
		if (abs(deltaUV[axis]) < 1e-8f) { if (uvStart[axis] < 0 || uvStart[axis] >= 1) return result; }
		else {
			float2 interval = float2(-uvStart[axis], 1 - uvStart[axis]) / deltaUV[axis];
			low = max(low, min(interval.x, interval.y));
			high = min(high, max(interval.x, interval.y));
		}
	}
	if (low > high) return result;
	uint steps = max(1u, (uint) ceil(max(abs(deltaUV.x) * depthParameters.x, abs(deltaUV.y) * depthParameters.y) * (high - low)));
	for (uint i = 0; i <= steps; ++i) {
		float2 uv = uvStart + deltaUV * lerp(low, high, float(i) / steps);
		int2 pixel = clamp((int2) (uv * depthParameters.xy), 0, (int2) depthParameters.xy - 1);
		float depth = gGPUCollisionDepth.Load(int3(pixel, 0));
		if (depth >= 1 || depth < 0) continue;
		int2 px = pixel + int2(pixel.x + 1 < (int) depthParameters.x ? 1 : -1, 0);
		int2 py = pixel + int2(0, pixel.y + 1 < (int) depthParameters.y ? 1 : -1);
		float dx = gGPUCollisionDepth.Load(int3(px, 0));
		float dy = gGPUCollisionDepth.Load(int3(py, 0));
		if (dx >= 1 || dy >= 1) continue;
		float3 surface = ParticleDepthWorld(pixel, depth);
		float3 normal = ParticleNormalize(cross(ParticleDepthWorld(px, dx) - surface, ParticleDepthWorld(py, dy) - surface), float3(0, 1, 0));
		float4 nearClip = mul(float4(0, 0, 0, 1), depthInverseViewProjection);
		float3 nearPoint = nearClip.xyz / nearClip.w;
		if (dot(normal, nearPoint - surface) < 0) normal = -normal;
		float a = dot(start - surface, normal) - radius;
		float b = dot(end - surface, normal) - radius;
		if (a < -thickness || b >= a || b > 0) continue;
		float time = a <= 0 ? 0 : a / max(a - b, 1e-8f);
		float3 center = lerp(start, end, time);
		float4 clip = mul(float4(center, 1), depthViewProjection);
		float2 hitUV = float2(clip.x / clip.w * 0.5f + 0.5f, 0.5f - clip.y / clip.w * 0.5f);
		float2 pixelError = abs(hitUV * depthParameters.xy - float2(pixel) - 0.5f);
		if (any(pixelError > 1.5f)) continue;
		if (result.hit != 0 && time >= result.time) continue;
		result.hit = 1;
		result.time = time;
		result.position = center - normal * (radius + min(a, 0));
		result.normal = normal;
		result.uv = hitUV;
	}
	return result;
}

#endif
