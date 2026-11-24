#include "../Mesh/Common/meshShaderSharedTypes.hlsli"
#include "clothEnvironment.hlsli"

struct ClothSeed {
	float3 rest;
	uint pinned;
	float3 initial;
	float3 previous;
};
struct ClothLink {
	uint a, b, kind;
	float length;
};
StructuredBuffer<ClothSeed> gClothSeeds : register(t0);
StructuredBuffer<ClothLink> gClothLinks : register(t1);
StructuredBuffer<uint2> gClothGroups : register(t2);
RWStructuredBuffer<float3> gClothPositions : register(u0);
RWStructuredBuffer<float3> gClothPrevious : register(u1);
RWStructuredBuffer<float3> gClothPredicted : register(u2);
RWStructuredBuffer<float> gClothLambda : register(u3);
RWStructuredBuffer<MeshVertex> gClothVertices : register(u4);

cbuffer ClothFrame : register(b0) {
	row_major float4x4 clothWorld;
	row_major float4x4 clothInverse;
	float3 windDirection;
	float windStrength;
	float3 compliance;
	float gravity;
	float drag, gustStrength, thickness;
	uint windEnabled;
	uint nx, ny, axis, groupCount;
	uint fieldCount, colliderCount, windMask, collisionMask;
};
cbuffer ClothPass : register(b1) {
	uint pass, first, count, iteration;
	float phase;
};
static const float dt = 1.0f / 120.0f;
static const uint threads = 128;
groupshared float3 smallPositions[512];
groupshared float smallLambda[3072];

float3 ClothUnit(float3 value, float3 fallback) {
	float squared = dot(value, value);
	return squared > 1e-18f ? value * rsqrt(squared) : fallback;
}

float3 ClothTriangleWind(uint a, uint b, uint c) {
	float3 p = gClothPositions[a], q = gClothPositions[b], r = gClothPositions[c];
	float3 n = ClothUnit(cross(q - p, r - p), 0);
	float gust = sin(phase + dot(gClothSeeds[a].rest.xy, float2(2.3f, 1.7f)));
	float3 velocity = ((p - gClothPrevious[a]) + (q - gClothPrevious[b]) + (r - gClothPrevious[c])) / (3 * dt);
	float speed = clamp(dot(windDirection * (windStrength + gustStrength * gust) - velocity, n), -50, 50);
	return n * (speed * abs(speed) / 6);
}

float3 ClothPredict(uint i) {
	ClothSeed seed = gClothSeeds[i];
	if (seed.pinned) return mul(float4(seed.rest, 1), clothWorld).xyz;
	uint stride = nx + 1, x = i % stride, y = i / stride;
	float3 force = float3(0, gravity, 0);
	// 頂点に接する面から風を集め、隣の頂点への同時加算を避ける
	if (windEnabled) {
		if (x < nx && y < ny) force += ClothTriangleWind(i, i + stride, i + 1);
		if (x > 0 && y < ny) {
			force += ClothTriangleWind(i - 1, i - 1 + stride, i);
			force += ClothTriangleWind(i, i - 1 + stride, i + stride);
		}
		if (x < nx && y > 0) {
			force += ClothTriangleWind(i - stride, i, i - stride + 1);
			force += ClothTriangleWind(i - stride + 1, i, i + 1);
		}
		if (x > 0 && y > 0) force += ClothTriangleWind(i - stride, i - 1, i);
	}
	float3 p = gClothPositions[i], delta = p - gClothPrevious[i];
	if (windMask) force += ClothFieldAcceleration(p, delta / dt, windMask, fieldCount, dt);
	return p + drag * delta + force * (dt * dt);
}

void ClothEnforce(ClothLink link, inout float3 a, inout float3 b, inout float lambda) {
	float wa = gClothSeeds[link.a].pinned ? 0 : 1;
	float wb = gClothSeeds[link.b].pinned ? 0 : 1;
	float3 delta = b - a;
	float len = length(delta);
	if (wa + wb == 0 || len < 1e-7f) return;
	float alpha = compliance[link.kind];
	float change = (link.length - len - alpha * lambda) / (wa + wb + alpha);
	lambda += change;
	float3 correction = delta * (change / len);
	a -= wa * correction;
	b += wb * correction;
}

float3 ClothProject(uint i, float3 p) {
	return !gClothSeeds[i].pinned && collisionMask ? ClothProjectColliders(p, thickness, collisionMask, colliderCount) : p;
}

// 小さい格子では色グループ間の同期をグループ共有メモリ内で完結させる
void ClothSolveSmall(uint tid) {
	uint n = (nx + 1) * (ny + 1);
	for (uint i = tid; i < n; i += threads) smallPositions[i] = ClothPredict(i);
	GroupMemoryBarrierWithGroupSync();
	for (uint sweep = 0; sweep < 6; ++sweep) {
		for (uint group = 0; group < groupCount; ++group) {
			uint2 range = gClothGroups[group];
			for (uint j = tid; j < range.y; j += threads) {
				uint k = range.x + j;
				ClothLink link = gClothLinks[k];
				float3 a = smallPositions[link.a], b = smallPositions[link.b];
				float lambda = sweep ? smallLambda[k] : 0;
				ClothEnforce(link, a, b, lambda);
				smallPositions[link.a] = a;
				smallPositions[link.b] = b;
				smallLambda[k] = lambda;
			}
			GroupMemoryBarrierWithGroupSync();
		}
		if (colliderCount && collisionMask) {
			for (uint v = tid; v < n; v += threads) smallPositions[v] = ClothProject(v, smallPositions[v]);
			GroupMemoryBarrierWithGroupSync();
		}
	}
	for (uint v = tid; v < n; v += threads) {
		gClothPrevious[v] = gClothPositions[v];
		gClothPositions[v] = smallPositions[v];
	}
}

float3 ClothLocal(uint i) { return mul(float4(gClothPositions[i], 1), clothInverse).xyz; }

void ClothPublish(uint i) {
	uint stride = nx + 1, x = i % stride, y = i / stride;
	float3 du = ClothLocal(i + (x < nx)) - ClothLocal(i - (x > 0));
	float3 dv = ClothLocal(i + stride * (y < ny)) - ClothLocal(i - stride * (y > 0));
	float sign = axis == 1 ? -1 : 1;
	MeshVertex v;
	v.position = float4(ClothLocal(i), 1);
	v.normal = ClothUnit(cross(du, dv) * sign, axis == 1 ? float3(0, 1, 0) : axis == 2 ? float3(1, 0, 0) : float3(0, 0, 1));
	v.tangent = ClothUnit(du - v.normal * dot(v.normal, du), axis == 2 ? float3(0, 1, 0) : float3(1, 0, 0));
	v.tangentSign = -sign;
	v.uv = float2(float(x) / nx, 1 - float(y) / ny);
	gClothVertices[i] = v;
}

[numthreads(128, 1, 1)]
void main(uint i : SV_DispatchThreadID, uint tid : SV_GroupIndex) {
	if (pass == 6) { ClothSolveSmall(tid); return; }
	if (i >= count) return;
	switch (pass) {
	case 0:
		gClothPositions[i] = gClothSeeds[i].initial;
		gClothPrevious[i] = gClothSeeds[i].previous;
		break;
	case 1:
		gClothPredicted[i] = ClothPredict(i);
		break;
	case 2: {
		uint k = first + i;
		ClothLink link = gClothLinks[k];
		float3 a = gClothPredicted[link.a], b = gClothPredicted[link.b];
		float lambda = iteration ? gClothLambda[k] : 0;
		ClothEnforce(link, a, b, lambda);
		gClothPredicted[link.a] = a;
		gClothPredicted[link.b] = b;
		gClothLambda[k] = lambda;
		break;
	}
	case 3:
		gClothPredicted[i] = ClothProject(i, gClothPredicted[i]);
		break;
	case 4:
		gClothPrevious[i] = gClothPositions[i];
		gClothPositions[i] = gClothPredicted[i];
		break;
	case 5: ClothPublish(i); break;
	}
}
