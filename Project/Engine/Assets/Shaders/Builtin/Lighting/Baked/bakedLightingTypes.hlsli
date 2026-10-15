#ifndef NEM_BAKED_LIGHTING_TYPES_HLSLI
#define NEM_BAKED_LIGHTING_TYPES_HLSLI

struct BakedVertex {

	float3 position;
	float lightmapU;
	float3 normal;
	float lightmapV;
	float2 uv;
	uint tangentOct;
	float tangentSign;
};
struct BakedTriangle {

	BakedVertex vertices[3];
	uint materialIndex;
	uint atlasIndex;
	uint chartIndex;
	uint receiverIndex;
};
struct BakedNode {

	float3 minimum;
	uint left;
	float3 maximum;
	uint right;
};
struct BakedTexel {

	float3 position;
	uint valid;
	float3 normal;
	uint chart;
};
struct BakedProbe {

	float3 position;
	float radius;
	float4 irradiance[9];
	float4 visibility[6];
};
struct BakedCapture {

	float3 position;
	float radius;
	float3 extents;
	uint shape;
	uint dataOffset;
	uint size;
	uint mipCount;
	float intensity;
	row_major float4x4 worldToBox;
};

// 二次までの実球面調和関数を評価する
void BakedSHBasis(float3 direction, out float basis[9]) {

	basis[0] = 0.282094792f;
	basis[1] = 0.488602512f * direction.y;
	basis[2] = 0.488602512f * direction.z;
	basis[3] = 0.488602512f * direction.x;
	basis[4] = 1.092548431f * direction.x * direction.y;
	basis[5] = 1.092548431f * direction.y * direction.z;
	basis[6] = 0.315391565f * (3.0f * direction.z * direction.z - 1.0f);
	basis[7] = 1.092548431f * direction.x * direction.z;
	basis[8] = 0.546274215f * (direction.x * direction.x - direction.y * direction.y);
}

uint BakedDirectionFace(float3 direction) {

	float3 absolute = abs(direction);
	if (absolute.x >= absolute.y && absolute.x >= absolute.z) return direction.x >= 0.0f ? 0u : 1u;
	if (absolute.y >= absolute.z) return direction.y >= 0.0f ? 2u : 3u;
	return direction.z >= 0.0f ? 4u : 5u;
}

#endif
