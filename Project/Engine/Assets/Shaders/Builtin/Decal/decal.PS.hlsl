//============================================================================
//	include
//============================================================================
#include "decalCommon.hlsli"
#include "../Mesh/Common/deferredGBuffer.hlsli"

Texture2D<float4> gDecalPosition : register(t0);
Texture2D<float4> gDecalOriginalNormal : register(t1);
Texture2D<uint> gDecalFlags : register(t2);
SamplerState gDecalSampler : register(s1);
cbuffer DecalDrawConstants : register(b0) {

	float3 cameraPosition;
	float pixelWorldScale;
	uint decalIndex;
	uint orthographic;
	uint2 _drawPad;
};

struct DecalOutput {

	float4 color : SV_TARGET0;
	float4 normal : SV_TARGET1;
	float4 surface : SV_TARGET2;
	float4 emissive : SV_TARGET3;
};

DecalOutput main(float4 position : SV_POSITION) {

	const int3 pixel = int3(uint2(position.xy), 0);
	const uint flags = gDecalFlags.Load(pixel);
	if ((flags & (kMaterialFlagSurface | kMaterialFlagReceiveDecal)) !=
		(kMaterialFlagSurface | kMaterialFlagReceiveDecal) || decalIndex >= gDecalCount) discard;
	const DecalGPUData decal = gDecals[decalIndex];
	if (((flags >> kRenderingLayerMaskShift) & decal.receiverMask) == 0) discard;
	const float3 worldPosition = gDecalPosition.Load(pixel).xyz;
	const float3 originalNormal = normalize(gDecalOriginalNormal.Load(pixel).xyz * 2.0f - 1.0f);
	const float footprint = pixelWorldScale * (orthographic != 0 ? 1.0f : length(worldPosition - cameraPosition));
	DecalSample decalSample;
	if (!EvaluateDecal(decal, worldPosition, originalNormal, footprint, gDecalSampler, decalSample)) discard;
	DecalOutput output;
	output.color = float4(decalSample.color, (decal.channels & 1u) ? decalSample.alpha : 0.0f);
	output.normal = float4(decalSample.normal * 0.5f + 0.5f, (decal.channels & 2u) ? decalSample.alpha : 0.0f);
	output.surface = float4(decalSample.surface, 0.0f, (decal.channels & 4u) ? decalSample.alpha : 0.0f);
	output.emissive = float4((decal.channels & 8u) ? decalSample.emissive * decalSample.alpha : 0.0f.xxx, 0.0f);
	return output;
}
