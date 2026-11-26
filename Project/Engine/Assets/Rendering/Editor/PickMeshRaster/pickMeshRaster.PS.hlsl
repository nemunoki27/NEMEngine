//============================================================================
//	include
//============================================================================
#include "../../Builtin/Mesh/Common/defaultMesh.hlsli"

SamplerState gSampler : register(s0);

//============================================================================
//	main
//============================================================================
uint4 main(VSOutput input) : SV_Target0 {

	if (!IsSubMeshRenderGroupVisible(input.instanceID, input.subMeshIndex)) discard;
	const MeshMaterialParameters material = GetInstanceMeshMaterialParameters(input.instanceID, input.subMeshIndex);
	if (0.0f < material.alphaClip) {
		const SubMeshShaderData subMesh = GetInstanceSubMesh(input.instanceID, input.subMeshIndex);
		const float2 uv = mul(float4(input.uv, 0.0f, 1.0f), subMesh.uvMatrix).xy;
		const HexTilingData hex = BuildHexTiling(uv, subMesh.hexTiling);
		float alpha = material.color.a;
		if (material.baseColorTexture != 0xFFFFFFFFu) {
			Texture2D<float4> texture = NEM_TEXTURE2D(material.baseColorTexture);
			alpha *= SampleHexGrad(texture, gSampler, hex, ddx(uv), ddy(uv), false).a;
		}
		if (material.opacityTexture != 0xFFFFFFFFu) {
			Texture2D<float4> texture = NEM_TEXTURE2D(material.opacityTexture);
			alpha *= SampleHexGrad(texture, gSampler, hex, ddx(uv), ddy(uv), false).r;
		}
		clip(alpha - material.alphaClip);
	}
	MeshInstance instance = gMeshInstances[input.instanceID];
	return uint4(
		instance.entityIndex,
		instance.entityGeneration,
		input.subMeshIndex,
		1u);
}
