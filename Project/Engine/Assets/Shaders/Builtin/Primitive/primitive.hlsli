#ifndef NEM_PRIMITIVE_HLSLI
#define NEM_PRIMITIVE_HLSLI

//============================================================================
//	Primitive 共有型
//	頂点はMeshVertexを共用し、描画とレイトレで同じバッファを読む
//============================================================================
cbuffer ViewConstants : register(b0) {

	float4x4 viewProjection;
	float3 cameraPosition;
	float _viewPad0;
};

struct PrimitiveInstance {

	float4x4 worldMatrix;
	float4x4 uvMatrix;
	float4 shapeParams0;
	float4 shapeParams1;
	float4 topColor;
	float4 centerColor;
	float4 bottomColor;
	uint flags;
	uint3 _pad;
};

float SmoothCylinderProfile(float t, float weight, bool pullEnd) {

	const float smoothT = t * t * (3.0f - 2.0f * t);
	const float power = 1.0f + saturate(weight) * 3.0f;
	return pullEnd ? 1.0f - pow(1.0f - smoothT, power) : pow(smoothT, power);
}

float4 ResolvePrimitiveVertexColor(float3 localPosition, PrimitiveInstance instance) {

	if (instance.shapeParams1.w < 0.5f) {
		return 1.0f.xxxx;
	}
	const float height = max(abs(instance.shapeParams0.w), 0.00001f);
	const float heightT = saturate(localPosition.y / height + 0.5f);
	if (heightT <= 0.5f) {

		const float t = SmoothCylinderProfile(heightT * 2.0f, instance.shapeParams1.y, false);
		return lerp(instance.bottomColor, instance.centerColor, t);
	}

	const float t = SmoothCylinderProfile((heightT - 0.5f) * 2.0f, instance.shapeParams1.x, true);
	return lerp(instance.centerColor, instance.topColor, t);
}

struct VSOutput {

	float4 position : SV_POSITION;
	float3 worldPos : TEXCOORD0;
	float3 normal : TEXCOORD1;
	float2 texcoord : TEXCOORD2;
	float3 tangent : TEXCOORD3;
	float tangentSign : TEXCOORD4;
	nointerpolation uint flags : TEXCOORD5;
	float4 vertexColor : COLOR0;
};

VSOutput BuildPrimitiveVertexOutput(
	float3 localPosition,
	float3 localNormal,
	float3 localTangent,
	float tangentSign,
	float2 uv,
	float4 vertexColor,
	PrimitiveInstance instance) {

	VSOutput output;
	float4 worldPosition = mul(
		float4(localPosition, 1.0f),
		instance.worldMatrix);
	output.position = mul(worldPosition, viewProjection);
	output.worldPos = worldPosition.xyz;
	output.normal = normalize(mul(
		localNormal, (float3x3)instance.worldMatrix));
	output.tangent = normalize(mul(
		localTangent, (float3x3)instance.worldMatrix));
	output.tangentSign = tangentSign;
	output.texcoord = mul(
		float4(uv, 0.0f, 1.0f),
		instance.uvMatrix).xy;
	output.flags = instance.flags;
	output.vertexColor = vertexColor;
	return output;
}

#endif // NEM_PRIMITIVE_HLSLI
