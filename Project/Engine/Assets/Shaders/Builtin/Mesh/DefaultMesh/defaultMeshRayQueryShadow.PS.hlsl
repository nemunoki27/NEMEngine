//============================================================================
//	include
//============================================================================
#include "../Common/defaultMesh.hlsli"
#include "../Common/meshLighting.hlsli"
#include "../Common/meshLambert.hlsli"

//============================================================================
//	output
//============================================================================
struct PSOutput {

	float4 color : SV_TARGET0;
	float4 normal : SV_TARGET1;
	float4 worldPos : SV_TARGET2;
};

struct TransparentPSOutput {

	float4 color : SV_TARGET0;
};

//============================================================================
//	InlineRayQueryによる平行光源シャドウ
//============================================================================
cbuffer RaytracingViewConstants : register(b4) {

	float4x4 rtView;
	float4x4 rtProjection;
	float4x4 rtInverseView;
	float4x4 rtInverseProjection;
	float4x4 rtInverseViewProjection;

	float3 rtCameraPosition;
	float rtMaxReflectionDistance;

	float2 rtRenderSize;
	float2 rtInvRenderSize;

	float rtShadowNormalBias;
	float rtReflectionIntensity;
	float rtNearClip;
	float rtFarClip;
};
RaytracingAccelerationStructure gSceneTLAS : register(t10);

// 平行光源方向へシャドウレイを飛ばし、遮蔽されていればtrueを返す
bool TraceDirectionalShadow(float3 worldPos, float3 worldNormal, float3 lightDirection) {

	RayDesc rayDesc;
	rayDesc.Origin = worldPos + worldNormal * rtShadowNormalBias;
	rayDesc.Direction = normalize(-lightDirection);
	rayDesc.TMin = 0.001f;
	rayDesc.TMax = rtFarClip;

	RayQuery < 0 > rayQuery;

	rayQuery.TraceRayInline(gSceneTLAS, 0, 0xFF, rayDesc);
	while (rayQuery.Proceed()) {
	}
	return rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// シャドウ付きの平行光源、影レイ原点は摂動前の幾何法線でオフセットしてセルフシャドウのアクネを防ぐ
float3 EvaluateLambertDirectionalShadowed(DirectionalLight light, float3 worldPos, float3 N, float3 geometricNormal) {

	float3 L = normalize(-light.direction);
	float lambert = HalfLambert(N, L);

	float shadow = TraceDirectionalShadow(worldPos, geometricNormal, light.direction) ?
		(1.0f - light.shadowStrength) : 1.0f;

	return lambert * shadow * light.color.rgb * light.intensity;
}

//============================================================================
//	main
//============================================================================
PSOutput main(VSOutput input) {

	SubMeshShaderData subMesh = GetInstanceSubMesh(input.instanceID, input.subMeshIndex);
	MeshMaterialParameters params = GetInstanceMeshMaterialParameters(input.instanceID, input.subMeshIndex);

	float2 uv = mul(float4(input.uv, 0.0f, 1.0f), subMesh.uvMatrix).xy;
	float4 baseColor = ResolveLambertBaseColor(params, uv);

	// 影レイ用の幾何法線は法線マップ適用前、ライティング用は摂動後のワールド法線
	float3 geometricNormal = normalize(input.normal);
	float3 N = ComputeWorldNormal(input, params.normalTexture, uv);

	// 平行光源はInlineRayQueryシャドウ付き、そのあと点とスポット
	float3 lit = 0.0f.xxx;
	[loop]
	for (uint i = 0; i < directionalCount; ++i) {
		lit += EvaluateLambertDirectionalShadowed(gDirectionalLights[i], input.worldPos, N, geometricNormal);
	}
	lit += AccumulateLocalLambertLighting(input.worldPos, N);

	PSOutput output;
	output.color = ComposeLambertColor(params, uv, baseColor, lit);
	output.normal = float4(N * 0.5f + 0.5f, 1.0f);
	output.worldPos = float4(input.worldPos, 1.0f);
	return output;
}

TransparentPSOutput mainTransparent(VSOutput input) {

	PSOutput lit = main(input);

	TransparentPSOutput output;
	output.color = lit.color;
	return output;
}
