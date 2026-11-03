//============================================================================
//	include
//============================================================================
#include "analyticGrid.hlsli"

//============================================================================
//	output
//============================================================================
struct PSOutput {
	
	float4 color : SV_TARGET0;
	float depth : SV_Depth;
};

//============================================================================
//	resources
//============================================================================
cbuffer GridPassConstants : register(b0) {

	float4x4 inverseRelativeViewProjectionMatrix;
	float4x4 relativeViewProjectionMatrix;

	float4 cameraPositionAndPlaneY;
	float4 viewportSize;
	float4 stepData0;
	float4 stepData1;
	float4 thicknessFadeAndHorizon;

	float4 minorColor;
	float4 minorParams0;
	float4 minorParams1;

	float4 majorColor;
	float4 majorParams0;
	float4 majorParams1;

	float4 coarseColor;
	float4 coarseParams0;
	float4 coarseParams1;

	float4 axisXColor;
	float4 axisZColor;
	float4 axisParams;
};

//============================================================================
//	functions
//============================================================================
float Smoother01(float x) {

	x = saturate(x);
	return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

float SmootherStep(float a, float b, float x) {

	return Smoother01((x - a) / max(b - a, 1e-6f));
}

float ComputeDistanceFade(float distanceXZ, float startDistance, float endDistance, float power) {

	float safeEndDistance = max(endDistance, startDistance + 1e-5f);
	float t = saturate((distanceXZ - startDistance) / (safeEndDistance - startDistance));

	float fade = 1.0f - Smoother01(t);
	return pow(max(fade, 0.0f), max(power, 0.01f));
}

float ComputeEffectivePixelWidth(float baseHalfThickness,
	float farThicknessRate,
	float fadeFactor,
	float thicknessFadePower,
	float minHalfThickness) {

	float safeBaseHalfThickness = max(baseHalfThickness, 0.01f);
	float safeFarThicknessRate = saturate(farThicknessRate);
	float safeMinHalfThickness = max(minHalfThickness, 0.01f);

	float thicknessT = pow(saturate(fadeFactor), max(thicknessFadePower, 0.01f));
	float halfThickness = lerp(safeBaseHalfThickness * safeFarThicknessRate, safeBaseHalfThickness, thicknessT);
	halfThickness = max(halfThickness, safeMinHalfThickness);

	return halfThickness * 2.0f;
}

float ComputePixelCoverage(float distancePixels, float pixelWidth) {

	float width = max(pixelWidth, 1.0f);
	return saturate(min(width * 0.5f + 0.75f - distancePixels, width) / 1.5f);
}

float ComputeRepeatingGridCoverage(float2 relativeXZ, float2 worldDerivative, float step, float pixelWidth) {

	float safeStep = max(step, 1e-6f);
	float2 deriv = max(worldDerivative / safeStep, float2(1e-6f, 1e-6f));
	// 原点からの位相を保ちながら小さい相対座標で周期を計算する
	float2 coord = relativeXZ / safeStep + frac(cameraPositionAndPlaneY.xz / safeStep);
	float2 distancePixels = abs(frac(coord + 0.5f) - 0.5f) / deriv;
	float2 cellPixels = 1.0f / deriv;
	float lineX = ComputePixelCoverage(distancePixels.x, pixelWidth) * SmootherStep(2.0f, 8.0f, cellPixels.x);
	float lineZ = ComputePixelCoverage(distancePixels.y, pixelWidth) * SmootherStep(2.0f, 8.0f, cellPixels.y);
	return lineX + lineZ - lineX * lineZ;
}

float ComputeCleanCrossFadedCoverage(float2 relativeXZ, float2 worldDerivative,
	float step0, float step1, float blend, float pixelWidth) {

	float coverage0 = ComputeRepeatingGridCoverage(relativeXZ, worldDerivative, step0, pixelWidth);
	float coverage1 = ComputeRepeatingGridCoverage(relativeXZ, worldDerivative, step1, pixelWidth);
	return lerp(coverage0, coverage1, Smoother01(blend));
}

float ComputeAxisCoverage(float coord, float deriv, float pixelWidth) {

	return ComputePixelCoverage(abs(coord) / max(deriv, 1e-6f), pixelWidth);
}

float SafeReciprocal(float value) {

	return (value < 0.0f ? -1.0f : 1.0f) / max(abs(value), 1e-6f);
}

void BlendLayer(inout float4 accumPremul, float3 rgb, float alpha) {

	alpha = saturate(alpha);
	float oneMinus = 1.0f - accumPremul.a;

	accumPremul.rgb += rgb * alpha * oneMinus;
	accumPremul.a += alpha * oneMinus;
}

//============================================================================
//	main
//============================================================================
PSOutput main(VSOutput input) {

	PSOutput output;
	
	output.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
	output.depth = 1.0f;

	float2 safeViewport = max(viewportSize.xy, float2(1.0f, 1.0f));
	float2 uv = input.position.xy / safeViewport;
	float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

	float4 nearRelative = mul(float4(ndc, 0.0f, 1.0f), inverseRelativeViewProjectionMatrix);
	float4 middleRelative = mul(float4(ndc, 0.5f, 1.0f), inverseRelativeViewProjectionMatrix);
	nearRelative.xyz *= SafeReciprocal(nearRelative.w);
	middleRelative.xyz *= SafeReciprocal(middleRelative.w);

	float3 cameraPosition = cameraPositionAndPlaneY.xyz;
	float gridPlaneY = cameraPositionAndPlaneY.w - cameraPosition.y;

	float3 ray = middleRelative.xyz - nearRelative.xyz;
	float3 rayDirection = ray / max(length(ray), 1e-6f);
	float denom = rayDirection.y;

	// 平行投影でも画面位置ごとに視線を分ける
	float t = (gridPlaneY - nearRelative.y) * SafeReciprocal(denom);
	float3 relativePosition = nearRelative.xyz + rayDirection * t;

	// 全画素で先に微分を求めて破棄画素の境界でも線幅を安定させる
	float2 worldDDX = ddx_fine(relativePosition.xz);
	float2 worldDDY = ddy_fine(relativePosition.xz);
	float2 worldDerivative = sqrt(worldDDX * worldDDX + worldDDY * worldDDY);
	float4 gridClip = mul(float4(relativePosition, 1.0f), relativeViewProjectionMatrix);
	float gridDepth = gridClip.z * SafeReciprocal(gridClip.w);
	if (abs(denom) < 1e-6f || t <= 0.0f || gridClip.w <= 1e-6f || gridDepth < 0.0f || 1.0f < gridDepth) {
		discard;
	}

	float distanceXZ = length(relativePosition.xz);

	float horizonFade = SmootherStep(
	thicknessFadeAndHorizon.z,
	thicknessFadeAndHorizon.w,
	abs(rayDirection.y));
	
	float stepBlend = saturate(stepData1.w);

	float4 accumPremul = float4(0.0f, 0.0f, 0.0f, 0.0f);

	//============================================================================
	//	minor
	//============================================================================
	{
		float fade = ComputeDistanceFade(distanceXZ, minorParams0.z, minorParams0.w, minorParams1.x) * horizonFade;
		float pixelWidth = ComputeEffectivePixelWidth(
			minorParams0.x,
			minorParams0.y,
			fade,
			thicknessFadeAndHorizon.x,
			thicknessFadeAndHorizon.y);

		float coverage = ComputeCleanCrossFadedCoverage(
			relativePosition.xz,
			worldDerivative,
			stepData0.x,
			stepData1.x,
			stepBlend,
			pixelWidth);

		float alpha = minorColor.a * fade * coverage;
		BlendLayer(accumPremul, minorColor.rgb, alpha);
	}

	//============================================================================
	//	major
	//============================================================================
	{
		float fade = ComputeDistanceFade(distanceXZ, majorParams0.z, majorParams0.w, majorParams1.x) * horizonFade;
		float pixelWidth = ComputeEffectivePixelWidth(
			majorParams0.x,
			majorParams0.y,
			fade,
			thicknessFadeAndHorizon.x,
			thicknessFadeAndHorizon.y);

		float coverage = ComputeCleanCrossFadedCoverage(
			relativePosition.xz,
			worldDerivative,
			stepData0.y,
			stepData1.y,
			stepBlend,
			pixelWidth);

		float alpha = majorColor.a * fade * coverage;
		BlendLayer(accumPremul, majorColor.rgb, alpha);
	}

	//============================================================================
	//	coarse
	//============================================================================
	{
		float fade = ComputeDistanceFade(distanceXZ, coarseParams0.z, coarseParams0.w, coarseParams1.x) * horizonFade;
		float pixelWidth = ComputeEffectivePixelWidth(
			coarseParams0.x,
			coarseParams0.y,
			fade,
			thicknessFadeAndHorizon.x,
			thicknessFadeAndHorizon.y);

		float coverage = ComputeCleanCrossFadedCoverage(
			relativePosition.xz,
			worldDerivative,
			stepData0.z,
			stepData1.z,
			stepBlend,
			pixelWidth);

		float alpha = coarseColor.a * fade * coverage;
		BlendLayer(accumPremul, coarseColor.rgb, alpha);
	}

	//============================================================================
	//	axis
	//============================================================================
	{
		float axisVisibleDistance = max(axisParams.w, 1.0f);
		float axisFade = ComputeDistanceFade(distanceXZ, 0.0f, axisVisibleDistance, 1.0f) * horizonFade;
		float axisPixelWidth = max(axisParams.x * 2.0f, thicknessFadeAndHorizon.y * 2.0f);

		// z = 0 が X axis
		float axisXCoverage = ComputeAxisCoverage(relativePosition.z + cameraPosition.z, worldDerivative.y, axisPixelWidth);
		float axisXAlpha = axisXColor.a * axisFade * axisXCoverage;
		BlendLayer(accumPremul, axisXColor.rgb, axisXAlpha);

		// x = 0 が Z axis
		float axisZCoverage = ComputeAxisCoverage(relativePosition.x + cameraPosition.x, worldDerivative.x, axisPixelWidth);
		float axisZAlpha = axisZColor.a * axisFade * axisZCoverage;
		BlendLayer(accumPremul, axisZColor.rgb, axisZAlpha);
	}

	if (accumPremul.a <= 1e-6f) {
		discard;
	}

	output.color.a = accumPremul.a;
	output.color.rgb = accumPremul.rgb / max(accumPremul.a, 1e-6f);
	output.depth = gridDepth;

	return output;
}
