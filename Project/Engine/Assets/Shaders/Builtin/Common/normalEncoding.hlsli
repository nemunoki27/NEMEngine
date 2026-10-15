#ifndef NEM_NORMAL_ENCODING_HLSLI
#define NEM_NORMAL_ENCODING_HLSLI

// 圧縮した法線と接線を同じ形式で復元する
float3 DecodePackedDirection(uint packed) {

	int x = (int)(packed << 16) >> 16;
	int y = (int)packed >> 16;
	float2 coordinate = float2(x, y) / 32767.0f;
	float3 normal = float3(coordinate, 1.0f - abs(coordinate.x) - abs(coordinate.y));
	if (normal.z < 0.0f) {
		float2 old = normal.xy;
		normal.x = (1.0f - abs(old.y)) * (old.x >= 0.0f ? 1.0f : -1.0f);
		normal.y = (1.0f - abs(old.x)) * (old.y >= 0.0f ? 1.0f : -1.0f);
	}
	return normalize(normal);
}

#endif
