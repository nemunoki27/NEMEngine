#ifndef NEM_PRIMITIVE_HLSLI
#define NEM_PRIMITIVE_HLSLI

#include "primitiveUV.hlsli"

//============================================================================
//	Primitive 共有型
//	頂点はMeshVertexを共用し、描画とレイトレで同じバッファを読む
//============================================================================
cbuffer ViewConstants : register(b0) {

	float4x4 viewProjection;
	float4x4 unjitteredViewProjection;
	float4x4 previousViewProjection;
	float3 cameraPosition;
	uint frameSerial;
	float materialPadding;
	uint unassignedMaterialTextureMask;
	float2 _viewPad;
	float4 geometryBounds;
};
// GraphのMaterial定数と標準定数を分ける
#ifndef NEM_PRIMITIVE_SHADER_GRAPH
cbuffer MaterialParameters : register(b3) {

	float4 color;
	float4 emissiveColor;
	float metallic;
	float roughness;
	float emissiveIntensity;
	float displacementMidpoint;
	float displacementScale;
	float3 _materialPad0;
};
#endif
Texture2D<float4> baseColorTexture : register(t0, space2);
Texture2D<float4> metallicRoughnessTexture : register(t1, space2);
Texture2D<float4> occlusionTexture : register(t2, space2);
Texture2D<float4> emissiveTexture : register(t3, space2);
Texture2D<float4> normalTexture : register(t4, space2);
Texture2D<float4> metallicTexture : register(t5, space2);
Texture2D<float4> roughnessTexture : register(t6, space2);
Texture2D<float4> displacementTexture : register(t7, space2);

#include "primitiveInstanceColor.hlsli"

struct VSOutput {

	float4 position : SV_POSITION;
	float3 worldPos : TEXCOORD0;
	float3 normal : TEXCOORD1;
	float2 texcoord : TEXCOORD2;
	float3 tangent : TEXCOORD3;
	float tangentSign : TEXCOORD4;
	nointerpolation uint flags : TEXCOORD5;
	float4 vertexColor : COLOR0;
	float4 currentClipPosition : TEXCOORD6;
	float4 previousClipPosition : TEXCOORD7;
	nointerpolation uint entityIndex : TEXCOORD8;
	nointerpolation uint entityGeneration : TEXCOORD9;
	float4 uvCoordinates : TEXCOORD10;
	nointerpolation float4 ringParams : TEXCOORD11;
	nointerpolation float4 uvBasis : TEXCOORD12;
	nointerpolation uint instanceID : TEXCOORD13;
	nointerpolation float4 hexTiling : TEXCOORD14;
};

VSOutput BuildPrimitiveVertexOutput(
	float3 localPosition,
	float3 localNormal,
	float3 localTangent,
	float tangentSign,
	float2 uv,
	float4 vertexColor,
	PrimitiveInstance instance, uint instanceID) {

	VSOutput output;
	output.instanceID = instanceID;
	output.hexTiling = instance.hexTiling;
	output.uvCoordinates = float4(localPosition.xy, uv);
	output.ringParams = instance.shapeParams1.z > 0.5f ? instance.shapeParams0 : 0.0f.xxxx;
	output.uvBasis = float4(instance.uvMatrix[0].xy, instance.uvMatrix[1].xy);
	const float2 transformedUV = mul(
		float4(uv, 0.0f, 1.0f), instance.uvMatrix).xy;
	// Graphの頂点変形へ標準Displacementを重ねない
#ifndef NEM_PRIMITIVE_SHADER_GRAPH
	if (abs(displacementScale) > 0.000001f) {

		const float height = SampleHexDisplacement(displacementTexture,
			BuildHexTiling(transformedUV, instance.hexTiling));
		localPosition += normalize(localNormal) *
			((height - displacementMidpoint) * displacementScale);
	}
#endif
	float4 worldPosition = mul(
		float4(localPosition, 1.0f),
		instance.worldMatrix);
	output.position = mul(worldPosition, viewProjection);
	output.currentClipPosition = mul(worldPosition, unjitteredViewProjection);
	float4x4 previousWorldMatrix =
		instance.motionFrameSerial == frameSerial ?
		instance.previousWorldMatrix : instance.worldMatrix;
	float4 previousWorldPosition = mul(
		float4(localPosition, 1.0f), previousWorldMatrix);
	output.previousClipPosition = mul(
		previousWorldPosition, previousViewProjection);
	output.worldPos = worldPosition.xyz;
	float orientationSign;
	output.normal = TransformPrimitiveNormal(localNormal, (float3x3)instance.worldMatrix, orientationSign);
	// 裏側から見える板も光の当たる面を正しく計算する
	const float facingSign = instance.shapeParams1.w > 1.5f &&
		dot(output.normal, cameraPosition - output.worldPos) < 0.0f ? -1.0f : 1.0f;
	output.normal *= facingSign;
	output.tangent = normalize(mul(
		localTangent, (float3x3)instance.worldMatrix));
	output.tangentSign = tangentSign * orientationSign * facingSign;
	output.texcoord = transformedUV;
	output.flags = instance.flags;
	output.vertexColor = vertexColor;
	output.entityIndex = instance.entityIndex;
	output.entityGeneration = instance.entityGeneration;
	return output;
}

VSOutput BuildPrimitiveVertexOutput(
	float3 localPosition, float3 localNormal, float3 localTangent, float tangentSign,
	float2 uv, float4 vertexColor, PrimitiveInstance instance) {

	return BuildPrimitiveVertexOutput(localPosition, localNormal, localTangent, tangentSign, uv, vertexColor, instance, 0u);
}

#endif // NEM_PRIMITIVE_HLSLI
