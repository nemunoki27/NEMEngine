#include "particleGPUView.hlsli"
#include "particleGPUDraw.hlsli"
#define NEM_TRAIL_CONSTANT_REGISTER b1
#include "particleGPUTrail.hlsli"
#include "particleGPUTrailRead.hlsli"

StructuredBuffer<uint2> gGPUTrailSegments : register(t3);

ParticleGPUVertexOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
	uint encoded = gGPUTrailSegments[instanceID].y;
	uint2 segment = uint2(encoded / pointCapacity, encoded % pointCapacity);
	ParticleTrailGPU trail = gGPUTrails[segment.x];
	uint count = ParticleTrailPointCount(trail);
	const float2 corners[6] = {float2(0, -1), float2(0, 1), float2(1, -1), float2(1, -1), float2(0, 1), float2(1, 1)};
	float2 corner = corners[vertexID];
	uint ordinal = segment.y + (uint) corner.x;
	ParticleTrailPointGPU current = ReadParticleTrailPoint(trail, segment.x, ordinal);
	float3 previous = ReadParticleTrailPoint(trail, segment.x, ordinal == 0 ? 0 : ordinal - 1).positionAge.xyz;
	float3 next = ReadParticleTrailPoint(trail, segment.x, min(ordinal + 1, count - 1)).positionAge.xyz;
	float3 tangent = ParticleNormalize(next - previous, float3(0, 1, 0));
	float3 viewDirection = cameraForward.w != 0 ? -cameraForward.xyz : cameraPosition.xyz - current.positionAge.xyz;
	viewDirection = ParticleNormalize(viewDirection, float3(0, 0, -1));
	float3 side = ParticleNormalize(cross(tangent, viewDirection), float3(1, 0, 0));
	float progress = float(ordinal) / max(count - 1, 1u);
	float width = lerp(endWidth, startWidth, progress) * 0.5f;
	float3 position = current.positionAge.xyz + side * width * corner.y;
	float lifetimeProgress = saturate(current.positionAge.w / max(lifetime, 0.001f));
	float4 color = current.color * lerp(endColor, startColor, progress);
	color.a *= 1 - lifetimeProgress;
	float u = progress;
	if (textureMode != 0) u = (current.distance - ReadParticleTrailPoint(trail, segment.x, 0).distance) / tileLength;
	ParticleGPUVertexOutput output;
	output.position = mul(float4(position, 1), viewProjection);
	output.uv = float2(u, corner.y * 0.5f + 0.5f);
	output.color = color;
	// CPUと同じくセグメント両端の値を使う
	ParticleTrailPointGPU first = ReadParticleTrailPoint(trail, segment.x, segment.y);
	ParticleTrailPointGPU last = ReadParticleTrailPoint(trail, segment.x, segment.y + 1);
	output.emissive = (first.emissive + last.emissive) * 0.5f;
	output.alphaReference = (first.reserved + last.reserved) * 0.5f;
	output.blendMode = blendMode;
	return output;
}
