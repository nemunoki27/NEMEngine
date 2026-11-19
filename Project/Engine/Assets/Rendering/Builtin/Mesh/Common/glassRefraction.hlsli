// 不透明描画の結果をガラス内部の屈折方向で参照する
float4 ApplyGlassRefraction(float4 surface, ShaderGraphSurface graph, float2 pixel, float3 worldPos) {
	uint width, height;
	gShaderGraphSceneColor.GetDimensions(width, height);
	float2 size = max(float2(width, height), 1.0f);
	float3 incident = shaderGraphCameraProjection.x != 0.0f ? normalize(shaderGraphCameraDirection.xyz) :
		normalize(worldPos - shaderGraphCameraPosition.xyz);
	float3 normal = faceforward(normalize(graph.normal), incident, graph.normal);
	float3 direction = refract(incident, normal, 1.0f / 1.5f);
	float4 projected = mul(float4(worldPos + direction * 0.12f, 1.0f), shaderGraphWorldToClip);
	float2 uv = pixel / size;
	if (projected.w > 0.0001f) {
		float2 localUV = projected.xy / projected.w * float2(0.5f, -0.5f) + 0.5f;
		uv = (shaderGraphViewport.xy + localUV * shaderGraphViewport.zw) / size;
	}
	uv = clamp(uv, (shaderGraphViewport.xy + 0.5f) / size,
		(shaderGraphViewport.xy + shaderGraphViewport.zw - 0.5f) / size);
	float3 background = gShaderGraphSceneColor.SampleLevel(gSampler, uv, 0).rgb;
	float transmitted = (1.0f - surface.a) * graph.transmission;
	float alpha = surface.a + transmitted;
	return float4((surface.rgb * surface.a + background * graph.transmissionColor * transmitted) / max(alpha, 0.0001f), alpha);
}
