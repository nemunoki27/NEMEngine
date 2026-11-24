//============================================================================
//	include
//============================================================================
#include "lineGeometry.hlsli"
#include "../../Atmosphere/transparentFog.hlsli"

//============================================================================
//	main
//============================================================================
float4 DebugLine(GSOutput input) : SV_TARGET0 {

	float safeOuter = max(input.outerHalfWidth, 1e-4f);
	float safeInner = clamp(input.halfWidth, 0.0f, safeOuter);

	float distanceFromCenter = abs(input.side) * safeOuter;

	// 内側は完全不透明、外側フェザー領域だけ滑らかに減衰
	float coverage = 1.0f - smoothstep(safeInner, safeOuter, distanceFromCenter);

	float4 color = input.color;
	color.a *= coverage;

	clip(color.a - (1.0f / 255.0f));
	return color;
}

float4 HiddenDebugLine(GSOutput input) : SV_TARGET0 {

	float4 color = DebugLine(input);
	color.a *= 0.25f;
	return color;
}

float4 main(GSOutput input) : SV_TARGET0 {

	return ApplyTransparentFog(DebugLine(input), input.position);
}
