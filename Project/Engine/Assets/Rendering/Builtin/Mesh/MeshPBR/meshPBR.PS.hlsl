//============================================================================
//	include
//============================================================================
#include "../Common/defaultMesh.hlsli"
#include "../Common/meshLighting.hlsli"
#include "../Common/meshPBRMaterial.hlsli"
#include "../Common/meshPBR.hlsli"
#include "../Common/deferredGBuffer.hlsli"
#define NEM_MATERIAL_PAINT_RASTER
#include "../../Common/materialPaint.hlsli"

//============================================================================
//	output
//============================================================================
GBufferOutput EncodeMeshPBRGBuffer(
	VSOutput input, ResolvedPBRMaterial material) {

	MeshSurface surface;
	surface.albedo = material.baseColor.rgb;
	surface.normal = material.N;
	surface.worldPos = input.worldPos;
	surface.metallic = material.metallic;
	surface.roughness = material.roughness;
	surface.occlusion = material.ao;
	surface.emissive = material.emissive;
	surface.motion = ComputeGBufferMotion(
		input.currentClipPosition, input.previousClipPosition);
	// MeshRendererのフラグをGBufferへ渡してライティングパスで分岐させる
	surface.flags = BuildMaterialFlags(gMeshInstances[input.instanceID].flags);
	return EncodeGBuffer(surface);
}

//============================================================================
//	main
//============================================================================
GBufferOutput main(VSOutput input, uint primitive : SV_PrimitiveID) {

	ResolvedPBRMaterial m = ResolvePBRMaterial(input);
	MeshInstance instance = gMeshInstances[input.instanceID];
	if (HasMaterialPaint(instance.flags)) {
		float2 uv = mul(float4(input.uv,0,1),GetInstanceSubMesh(input.instanceID,input.subMeshIndex).uvMatrix).xy;
		PaintFootprint f = {ddx(input.worldPos),ddy(input.worldPos),0};
		ApplyMaterialPaint(uint2(instance.entityIndex,instance.entityGeneration),primitive,meshIndexOffset/3,
			input.worldPos,input.normal,uv,ddx(uv),ddy(uv),f,gSampler,m.baseColor.rgb,m.N,m.roughness,m.metallic,m.ao,m.emissive);
	}
	return EncodeMeshPBRGBuffer(input, m);
}

GBufferOutput mainMasked(VSOutput input) {

	ResolvedPBRMaterial material = ResolvePBRMaterial(input);
	clip(material.baseColor.a - ResolveMeshPBRAlphaClip(input));
	return EncodeMeshPBRGBuffer(input, material);
}

void mainDepth(VSOutput input) {

	ResolvedPBRMaterial material = ResolvePBRMaterial(input);
	clip(material.baseColor.a - ResolveMeshPBRAlphaClip(input));
}
