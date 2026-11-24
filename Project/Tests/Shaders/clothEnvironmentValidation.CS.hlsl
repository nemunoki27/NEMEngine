#include "../../Engine/Assets/Rendering/Builtin/Cloth/clothEnvironment.hlsli"

struct ClothProbe {
	float3 position;
	uint mask;
	float3 velocity;
	uint mode;
	uint fields;
	uint colliders;
	uint count;
	float thickness;
};

StructuredBuffer<ClothProbe> gClothProbes : register(t0);
RWStructuredBuffer<float3> gClothResults : register(u0);

[numthreads(64, 1, 1)]
void main(uint index : SV_DispatchThreadID) {
	if (index >= gClothProbes[0].count) return;
	ClothProbe probe = gClothProbes[index];
	if (probe.mode == 0)
		gClothResults[index] = ClothFieldAcceleration(probe.position, probe.velocity, probe.mask, probe.fields, 1.0f / 120);
	else
		gClothResults[index] = ClothProjectColliders(probe.position, probe.thickness, probe.mask, probe.colliders);
}
