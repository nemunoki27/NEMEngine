//============================================================================
//	include
//============================================================================
#include "primitive.hlsli"
#include "../Common/normalEncoding.hlsli"
#include "../Atmosphere/transparentFog.hlsli"
#include "../Mesh/Common/pbrShading.hlsli"
#include "../Mesh/Common/deferredGBuffer.hlsli"
#include "../Particle/GPU/particlePaintRead.hlsli"
#define NEM_MATERIAL_PAINT_RASTER
#include "../Common/materialPaint.hlsli"

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
	const HexTilingData hex = BuildHexTiling(uv, input.hexTiling);
	float4 baseColor = SamplePrimitiveMaterialTexture(baseColorTexture, gSampler, hex,
		unassignedMaterialTextureMask, 0u) * color * input.vertexColor;
	ApplyParticlePaint(uint2(input.entityIndex, input.entityGeneration), input.texcoord, input.worldPos, gSampler, baseColor);

	// メタリックとラフネスはglTF流でB=metallic G=roughness
	float4 mrSample = SamplePrimitiveMaterialTexture(metallicRoughnessTexture, gSampler, hex, unassignedMaterialTextureMask, 1u);
	float metallicSample = SamplePrimitiveMaterialTexture(metallicTexture, gSampler, hex, unassignedMaterialTextureMask, 5u).r;
	float roughnessSample = SamplePrimitiveMaterialTexture(roughnessTexture, gSampler, hex, unassignedMaterialTextureMask, 6u).r;
	float resolvedMetallic = saturate(metallic * mrSample.b * metallicSample);
	float resolvedRoughness = max(
		saturate(roughness * mrSample.g * roughnessSample), 0.04f);

	float ao = SamplePrimitiveMaterialTexture(occlusionTexture, gSampler, hex, unassignedMaterialTextureMask, 2u).r;
	float3 emissive = emissiveColor.rgb * emissiveIntensity *
		SamplePrimitiveMaterialTexture(emissiveTexture, gSampler, hex, unassignedMaterialTextureMask, 3u).rgb;

	// 法線マップ、未設定は白テクスチャが入るのでフラット法線とみなして幾何法線を使う
	float3 N = normalize(input.normal);
	float3 normalSample = SamplePrimitiveMaterialTexture(normalTexture, gSampler, hex, unassignedMaterialTextureMask, 4u).rgb;
	if (min(min(normalSample.r, normalSample.g), normalSample.b) < 0.99f) {

		float3 T = OrthogonalizeTangent(input.tangent, N);
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
GBufferOutput main(VSOutput input, uint primitive : SV_PrimitiveID) {

	ResolvedPBRMaterial m = ResolvePrimitiveMaterial(input);
	if (HasMaterialPaint(input.flags)) {
		float2 uv = ResolvePrimitivePixelUV(input.texcoord,input.uvCoordinates,input.ringParams,input.uvBasis);
		PaintFootprint f = {ddx(input.worldPos),ddy(input.worldPos),0};
		ApplyMaterialPaint(uint2(input.entityIndex,input.entityGeneration),primitive,0,input.worldPos,input.normal,
			uv,ddx(uv),ddy(uv),f,gSampler,m.baseColor.rgb,m.N,m.roughness,m.metallic,m.ao,m.emissive);
	}

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
		if (!IsLightRenderingLayerEnabled(gDirectionalLights[i].renderingLayerMask, input.flags >> 8u)) continue;
			Lo += EvaluatePBRDirectionalLight(gDirectionalLights[i], m.N, V, m.baseColor.rgb, m.metallic, m.roughness, F0);
		}
		[loop]
		for (uint pi = 0; pi < pointCount; ++pi) {
		if (!IsLightRenderingLayerEnabled(gPointLights[pi].renderingLayerMask, input.flags >> 8u)) continue;
			Lo += EvaluatePBRPointLight(gPointLights[pi], input.worldPos, m.N, V, m.baseColor.rgb, m.metallic, m.roughness, F0);
		}
		[loop]
		for (uint si = 0; si < spotCount; ++si) {
		if (!IsLightRenderingLayerEnabled(gSpotLights[si].renderingLayerMask, input.flags >> 8u)) continue;
			Lo += EvaluatePBRSpotLight(gSpotLights[si], input.worldPos, m.N, V, m.baseColor.rgb, m.metallic, m.roughness, F0);
		}
		[loop]
		for (uint ri = 0; ri < rectCount; ++ri) {
		if (!IsLightRenderingLayerEnabled(gRectLights[ri].renderingLayerMask, input.flags >> 8u)) continue;
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
	output.color = ApplyTransparentFog(float4(finalColor, m.baseColor.a), input.position);
	return output;
}
