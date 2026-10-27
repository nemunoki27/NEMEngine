#ifndef NEM_PRIMITIVE_INSTANCE_COLOR
#define NEM_PRIMITIVE_INSTANCE_COLOR

struct PrimitiveInstance {

	float4x4 worldMatrix;
	float4x4 previousWorldMatrix;
	float4x4 uvMatrix;
	float4 shapeParams0;
	float4 shapeParams1;
	float4 topColor;
	float4 centerColor;
	float4 bottomColor;
	uint flags;
	uint motionFrameSerial;
	uint entityIndex;
	uint entityGeneration;
};

// 非等方Scaleと負Scaleを含む法線を逆転置行列で変換する
float3 TransformPrimitiveNormal(float3 localNormal, float3x3 world, out float orientationSign) {

	const float3 c0 = cross(world[1], world[2]);
	const float3 c1 = cross(world[2], world[0]);
	const float3 c2 = cross(world[0], world[1]);
	orientationSign = dot(world[0], c0) < 0.0f ? -1.0f : 1.0f;
	const float3 normal = mul(localNormal, float3x3(c0, c1, c2)) * orientationSign;
	const float lengthSquared = dot(normal, normal);
	return lengthSquared > 0.000000000001f ? normal * rsqrt(lengthSquared) : localNormal;
}

float SmoothCylinderProfile(float t, float weight, bool pullEnd) {

	const float smoothT = t * t * (3.0f - 2.0f * t);
	const float power = 1.0f + saturate(weight) * 3.0f;
	return pullEnd ? 1.0f - pow(1.0f - smoothT, power) : pow(smoothT, power);
}

float4 ResolvePrimitiveVertexColor(float3 localPosition, PrimitiveInstance instance) {

	if (instance.shapeParams1.w != 1.0f) {
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

#endif
