//============================================================================
//	include
//============================================================================
#include "primitive.hlsli"
#include "../Mesh/Common/pbrShading.hlsli"
#include "../Mesh/Common/deferredGBuffer.hlsli"
#include "../Particle/GPU/particlePaintRead.hlsli"

//============================================================================
//	output
//============================================================================
struct TransparentPSOutput {

	float4 color : SV_TARGET0;
};

//============================================================================
//	material
//============================================================================
ResolvedPBRMaterial ResolvePrimitiveMaterial(VSOutput input) {

	float2 uv = 0.0f.xx;
	// 表面Textureがすべて未割当ならRingの角度・半径の復元も不要
	[branch]
	if ((unassignedMaterialTextureMask & 0x7fu) != 0x7fu) {
		uv = ResolvePrimitivePixelUV(input.texcoord,
			input.uvCoordinates, input.ringParams, input.uvBasis);
	}

	// ベースカラー = マテリアル色 × テクスチャ
	float4 baseColor = SamplePrimitiveMaterialTexture(baseColorTexture, gSampler, uv,
		unassignedMaterialTextureMask, 0u) * color * input.vertexColor;
	ApplyParticlePaint(uint2(input.entityIndex, input.entityGeneration), input.texcoord, input.worldPos, gSampler, baseColor);

	// メタリックとラフネスはglTF流でB=metallic G=roughness
	float4 mrSample = SamplePrimitiveMaterialTexture(metallicRoughnessTexture, gSampler, uv, unassignedMaterialTextureMask, 1u);
	float metallicSample = SamplePrimitiveMaterialTexture(metallicTexture, gSampler, uv, unassignedMaterialTextureMask, 5u).r;
	float roughnessSample = SamplePrimitiveMaterialTexture(roughnessTexture, gSampler, uv, unassignedMaterialTextureMask, 6u).r;
	float resolvedMetallic = saturate(metallic * mrSample.b * metallicSample);
	float resolvedRoughness = max(
		saturate(roughness * mrSample.g * roughnessSample), 0.04f);

	float ao = SamplePrimitiveMaterialTexture(occlusionTexture, gSampler, uv, unassignedMaterialTextureMask, 2u).r;
	float3 emissive = emissiveColor.rgb * emissiveIntensity *
		SamplePrimitiveMaterialTexture(emissiveTexture, gSampler, uv, unassignedMaterialTextureMask, 3u).rgb;

	// 法線マップ、未設定は白テクスチャが入るのでフラット法線とみなして幾何法線を使う
	float3 N = normalize(input.normal);
	float3 normalSample = SamplePrimitiveMaterialTexture(normalTexture, gSampler, uv, unassignedMaterialTextureMask, 4u).rgb;
	if (min(min(normalSample.r, normalSample.g), normalSample.b) < 0.99f) {

		float3 T = normalize(input.tangent - N * dot(N, input.tangent));
		float3 B = cross(N, T) * input.tangentSign;
		float3 tangentNormal = normalSample * 2.0f - 1.0f;
		N = normalize(mul(tangentNormal, float3x3(T, B, N)));
	}

	ResolvedPBRMaterial m;
	m.baseColor = baseColor;
	m.N = N;
	m.metallic = resolvedMetallic;
	m.roughness = resolvedRoughness;
	m.ao = ao;
	m.emissive = emissive;
	return m;
}

//============================================================================
//	main
//============================================================================
GBufferOutput main(VSOutput input) {

	ResolvedPBRMaterial m = ResolvePrimitiveMaterial(input);

	MeshSurface surface;
	surface.albedo = m.baseColor.rgb;
	surface.normal = m.N;
	surface.worldPos = input.worldPos;
	surface.metallic = m.metallic;
	surface.roughness = m.roughness;
	surface.occlusion = m.ao;
	surface.emissive = m.emissive;
	surface.motion = ComputeGBufferMotion(
		input.currentClipPosition, input.previousClipPosition);
	// renderFlagsから影/IBL/反射の受け設定を反映する
	surface.flags = BuildMaterialFlags(input.flags);

	GBufferOutput output = EncodeGBuffer(surface);
	// Transparentフェーズのブレンドにα値を反映する、Opaqueのディファード照明はalbedo.rgbのみ使う
	output.albedo.a = m.baseColor.a;
	return output;
}

//============================================================================
//	mainTransparent
//============================================================================
TransparentPSOutput mainTransparent(VSOutput input) {

	ResolvedPBRMaterial m = ResolvePrimitiveMaterial(input);

	float3 finalColor;
	// ライティングしないサーフェイスはベースカラーと発光をそのまま出す
	if ((input.flags & MESH_INSTANCE_FLAG_LIGHTING) == 0u) {
		finalColor = m.baseColor.rgb + m.emissive;
	} else {

		float3 V = normalize(cameraPosition - input.worldPos);
		float3 F0 = lerp(0.04f.xxx, m.baseColor.rgb, m.metallic);

		float3 Lo = 0.0f.xxx;
		[loop]
		for (uint i = 0; i < directionalCount; ++i) {
			Lo += EvaluatePBRDirectionalLight(gDirectionalLights[i], m.N, V, m.baseColor.rgb, m.metallic, m.roughness, F0);
		}
		[loop]
		for (uint pi = 0; pi < pointCount; ++pi) {
			Lo += EvaluatePBRPointLight(gPointLights[pi], input.worldPos, m.N, V, m.baseColor.rgb, m.metallic, m.roughness, F0);
		}
		[loop]
		for (uint si = 0; si < spotCount; ++si) {
			Lo += EvaluatePBRSpotLight(gSpotLights[si], input.worldPos, m.N, V, m.baseColor.rgb, m.metallic, m.roughness, F0);
		}
		[loop]
		for (uint ri = 0; ri < rectCount; ++ri) {
			Lo += EvaluatePBRRectLight(gRectLights[ri], input.worldPos, m.N, V, m.baseColor.rgb, m.metallic, m.roughness, F0);
		}

		// 環境光はAOで減衰、環境光を受けないサーフェイスは加算しない
		float3 ambient = 0.0f.xxx;
		if ((input.flags & MESH_INSTANCE_FLAG_RECEIVE_IBL) != 0u) {
			ambient = 0.03f * m.baseColor.rgb * m.ao;
		}
		finalColor = Lo + ambient + m.emissive;
	}

	TransparentPSOutput output;
	output.color = float4(finalColor, m.baseColor.a);
	return output;
}
