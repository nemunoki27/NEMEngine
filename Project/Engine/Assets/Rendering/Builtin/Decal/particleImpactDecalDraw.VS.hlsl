#include "decalCommon.hlsli"

cbuffer ParticleImpactDecalViewConstants : register(b4, space4) {
	float4x4 impactViewProjection;
};

struct ImpactOutput {
	float4 position : SV_POSITION;
	nointerpolation uint slot : TEXCOORD0;
	float4 clipDistances : SV_ClipDistance0;
};

ImpactOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
	ImpactOutput output;
	output.slot = instanceID;
	// 未使用slotは投影行列を読む前に描画範囲から外す
	if (gGPUImpactDecals[instanceID].alive == 0) {
		output.position = float4(2, 2, 0, 1);
		output.clipDistances = -1.0f.xxxx;
		return output;
	}
	DecalGPUData decal = gGPUImpactDecals[instanceID].decal;
	float3 center = -decal.worldToDecal[3][0] * decal.tangentX.xyz -
		decal.worldToDecal[3][1] * decal.tangentY.xyz - decal.worldToDecal[3][2] * decal.direction.xyz;
	float2 minimum = 1.0f.xx, maximum = -1.0f.xx;
	uint visible = 0;
	for (uint corner = 0; corner < 8; ++corner) {
		float3 cornerPosition = center + decal.tangentX.xyz * ((corner & 1u) ? 0.5f : -0.5f) +
			decal.tangentY.xyz * ((corner & 2u) ? 0.5f : -0.5f) + decal.direction.xyz * ((corner & 4u) ? 0.5f : -0.5f);
		float4 projected = mul(float4(cornerPosition, 1), impactViewProjection);
		if (projected.w <= 1e-6f || projected.z <= 0) continue;
		float2 ndc = projected.xy / projected.w;
		minimum = min(minimum, ndc);
		maximum = max(maximum, ndc);
		++visible;
	}
	if (visible > 0 && visible < 8) {
		minimum = -1.0f.xx;
		maximum = 1.0f.xx;
	}
	minimum = clamp(minimum, -1.0f.xx, 1.0f.xx);
	maximum = clamp(maximum, -1.0f.xx, 1.0f.xx);
	float2 cornerUV = float2((vertexID << 1u) & 2u, vertexID & 2u);
	output.position = visible == 0 ? float4(2, 2, 0, 1) : float4(lerp(minimum, maximum, cornerUV), 0, 1);
	output.clipDistances = float4(output.position.xy - minimum, maximum - output.position.xy);
	return output;
}
