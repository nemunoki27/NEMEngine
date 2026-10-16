#ifndef NEM_LIGHT_ATTENUATION_HLSLI
#define NEM_LIGHT_ATTENUATION_HLSLI

// 通常描画とベイクで同じ距離減衰を使う
float ComputeDistanceAttenuation(float dist, float range, float decay) {

	if (range <= 0.0001f || dist >= range) {
		return 0.0f;
	}

	float x = saturate(dist / range);
	float smooth = 1.0f - x * x;
	smooth *= smooth;

	float d = max(decay, 0.0f);
	float distanceFalloff = 1.0f / max(pow(max(dist, 1.0f), d), 1.0f);

	return smooth * distanceFalloff;
}

// バーンドアの範囲と境界の鋭さを求める
float ComputeRectBarnAttenuation(RectLight light, float3 worldPos) {

	float3 fromLight = worldPos - light.pos;
	float forward = dot(fromLight, light.direction);
	if (forward <= 0.0f) {
		return 0.0f;
	}
	if (light.barnDoorLength <= 0.0001f) {
		return 1.0f;
	}

	float spread = tan(radians(clamp(
		light.barnDoorAngle, 0.0f, 89.0f))) * forward;
	float widthLimit = max(light.sourceWidth, 0.0f) * 0.5f + spread;
	float heightLimit = max(light.sourceHeight, 0.0f) * 0.5f + spread;
	float lateralX = abs(dot(fromLight, light.right));
	float lateralY = abs(dot(fromLight, light.up));
	float edgeSoftness = max(
		forward / (1.0f + light.barnDoorLength * 4.0f), 0.001f);

	float widthAttenuation =
		1.0f - smoothstep(widthLimit, widthLimit + edgeSoftness, lateralX);
	float heightAttenuation =
		1.0f - smoothstep(heightLimit, heightLimit + edgeSoftness, lateralY);
	return widthAttenuation * heightAttenuation;
}

// 面積積分が距離減衰を含むため、影響半径だけで打ち切る
float ComputeRectRangeAttenuation(RectLight light, float3 worldPos) {

	return ComputeDistanceAttenuation(length(worldPos - light.pos), light.attenuationRadius, 0.0f);
}

// 片面の総出力を単位面積の放射輝度へ変換する
float3 GetRectLightRadiance(RectLight light) {

	float area = light.sourceWidth * light.sourceHeight;
	if (light.sourceWidth <= 0.0f || light.sourceHeight <= 0.0f || area <= 1e-8f) return 0.0f;
	return light.color.rgb * max(light.intensity, 0.0f) / (3.14159265359f * area);
}

// 矩形面の一様サンプルを立体角の積分重みへ変換する
float3 EvaluateRectSampleIrradiance(RectLight light, float3 worldPos, float3 normal, float3 emitter) {

	float3 delta = emitter - worldPos;
	float distanceSquared = dot(delta, delta);
	if (distanceSquared <= 1e-10f) return 0.0f;
	float3 direction = delta * rsqrt(distanceSquared);
	float geometry = saturate(dot(normal, direction)) * saturate(dot(light.direction, -direction));
	float area = max(light.sourceWidth, 0.0f) * max(light.sourceHeight, 0.0f);
	float attenuation = ComputeRectRangeAttenuation(light, worldPos) * ComputeRectBarnAttenuation(light, worldPos);
	return GetRectLightRadiance(light) * geometry * area / distanceSquared * attenuation;
}

#endif
