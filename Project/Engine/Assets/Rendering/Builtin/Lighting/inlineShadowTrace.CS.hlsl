//============================================================================
//	include
//============================================================================
#include "lightShaderData.hlsli"
#include "inlineShadowParameters.hlsli"
#include "inlineShadowCommon.hlsli"
#include "../Mesh/Common/deferredGBuffer.hlsli"

Texture2D<float4> gNormal : register(t1);
Texture2D<float4> gWorldPos : register(t2);
Texture2D<uint> gFlags : register(t5);
#include "inlineShadowGeometry.hlsli"
StructuredBuffer<uint> gShadowLightSources : register(t19);
RWStructuredBuffer<float2> gShadowOutput : register(u0);
RWTexture2D<float> gShadowPreview : register(u3);

[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID) {

	uint2 pixel = dispatchID.xy;
	uint slot = dispatchID.z;
	if (any(pixel >= shadowSize) || slot >= shadowLightCount) return;
	if (slot == shadowPreviewLightIndex) gShadowPreview[pixel] = 1.0f;
	uint offset = GetShadowOffset(pixel, slot);
	uint flags = gFlags.Load(int3(pixel, 0));
	if ((flags & (kMaterialFlagSurface | kMaterialFlagLighting | kMaterialFlagReceiveShadow)) !=
		(kMaterialFlagSurface | kMaterialFlagLighting | kMaterialFlagReceiveShadow)) {

		gShadowOutput[offset] = 0.0f.xx;
		return;
	}
	float3 position = gWorldPos.Load(int3(pixel, 0)).xyz;
	float3 normal = GetShadowSurfaceNormal(pixel, position);
	uint index = gShadowLightSources[slot];
	float3 direction = 0.0f.xxx;
	float3 toLight = 0.0f.xxx;
	float lightDistance = shadowMaxDistance;
	float sourceRadius = 0.0f;
	float3 right = 0.0f.xxx;
	float3 up = 0.0f.xxx;
	float2 sourceSize = 0.0f.xx;
	bool directional = index < directionalCount;
	bool rectangular = index >= directionalCount + pointCount + spotCount;

	// 光源の形状と影を受ける範囲を解決
	if (directional) {

		DirectionalLight light = gDirectionalLights[index];
		direction = normalize(-light.direction);
		sourceRadius = tan(radians(min(light.shadowAngularRadius, 5.0f)));
	} else if (index < directionalCount + pointCount) {

		PointLight light = gPointLights[index - directionalCount];
		toLight = light.pos - position;
		lightDistance = length(toLight);
		if (lightDistance >= light.radius) { gShadowOutput[offset] = 0.0f.xx; return; }
		sourceRadius = light.shadowRadius;
	} else if (!rectangular) {

		SpotLight light = gSpotLights[index - directionalCount - pointCount];
		toLight = light.pos - position;
		lightDistance = length(toLight);
		if (lightDistance >= light.distance || dot(-toLight / max(lightDistance, 0.001f), light.direction) <= light.cosAngle) {
			gShadowOutput[offset] = 0.0f.xx; return;
		}
		sourceRadius = light.shadowRadius;
	} else {

		RectLight light = gRectLights[index - directionalCount - pointCount - spotCount];
		toLight = light.pos - position;
		lightDistance = length(toLight);
		if (lightDistance >= light.attenuationRadius || dot(-toLight, light.direction) <= 0.0f) {
			gShadowOutput[offset] = 0.0f.xx; return;
		}
		right = light.right;
		up = light.up;
		sourceSize = max(float2(light.sourceWidth, light.sourceHeight), 0.0f.xx);
		sourceRadius = max(sourceSize.x, sourceSize.y) * 0.5f;
	}
	if (!directional) direction = toLight / max(lightDistance, 0.001f);
	if (lightDistance <= 0.001f) {
		gShadowOutput[offset] = 0.0f.xx; return;
	}
	float3 tangent;
	float3 bitangent;
	BuildShadowBasis(direction, tangent, bitangent);
	float3 origin = position + normal * shadowNormalBias;
	uint count = sourceRadius > 0.0001f ? clamp(softShadowSampleCount, 1u, 4u) : 1u;
	uint seed = HashShadowSeed(pixel.x * 1973u + pixel.y * 9277u + index * 26699u + shadowFrameIndex * 104729u);
	float2 sampleShift = float2(seed & 65535u, seed >> 16u) / 65536.0f;
	float rotation = ResolveShadowRotation(pixel, index) + float(shadowFrameIndex & 255u) * 2.39996323f;
	float sinRotation;
	float cosRotation;
	sincos(rotation, sinRotation, cosRotation);
	float occlusion = 0.0f;
	float blockerDistance = 0.0f;

	// フレームごとに光源上の標本を変える
	[loop]
	for (uint sampleIndex = 0; sampleIndex < count; ++sampleIndex) {

		uint diskIndex = (seed + sampleIndex * (4u / count)) & 3u;
		float2 disk = RotateShadowDisk(kSoftShadowDisk[diskIndex], sinRotation, cosRotation);
		float2 rectangleSample = frac(float2(float(sampleIndex) / float(count), float(sampleIndex) * 0.61803399f) + sampleShift) - 0.5f;
		float3 sampleDirection = directional ? normalize(direction + (tangent * disk.x + bitangent * disk.y) * sourceRadius) :
			toLight + (rectangular ? right * rectangleSample.x * sourceSize.x + up * rectangleSample.y * sourceSize.y :
				(tangent * disk.x + bitangent * disk.y) * sourceRadius);
		float distance = directional ? shadowMaxDistance : length(sampleDirection);
		float3 rayDirection = normalize(sampleDirection);
		float hitDistance = 0.0f;
		// 面の裏側から来る光を遮蔽なしとして扱わない
		bool hit = dot(normal, rayDirection) <= 0.0f;
		if (!hit) hit = TraceShadowRay(origin, rayDirection, distance, hitDistance);
		occlusion += hit ? 1.0f : 0.0f;
		blockerDistance += hitDistance;
	}
	blockerDistance /= max(occlusion, 1.0f);
	float penumbra = directional ? blockerDistance * sourceRadius :
		blockerDistance * sourceRadius / max(lightDistance - blockerDistance, 0.01f);
	// 接触影を保ち、半影の範囲だけを平滑化
	float radius = sourceRadius > 0.0001f ? clamp(penumbra / GetShadowPixelSize(position), 1.5f, 32.0f) : 0.0f;
	gShadowOutput[offset] = float2(occlusion / float(count), radius);
	if (slot == shadowPreviewLightIndex) gShadowPreview[pixel] = 1.0f - occlusion / float(count);
}
