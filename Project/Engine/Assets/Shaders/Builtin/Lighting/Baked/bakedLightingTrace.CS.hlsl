#include "bakedLightingTraceCommon.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 dispatch : SV_DispatchThreadID) {

	if (dispatch.x >= taskCount) return;
	uint task = firstTask + dispatch.x;
	if (stage == 1u) {
		BakedProbe probe = (BakedProbe)0;
		if (sampleStart != 0u) probe = gBakeProbeOutput[task];
		probe.position = gBakeProbes[task].position;
		probe.radius = gBakeProbes[task].radius;
		for (uint sample = 0; sample < samplesPerTask; ++sample) {
			uint seed = BakeHash(task ^ BakeHash(sampleStart + sample + 0x4137u));
			float3 direction = BakeSphere(seed);
			float distance, backface;
			float3 radiance = BakeRadiance(probe.position, direction, seed, distance, backface);
			float basis[9];
			BakedSHBasis(direction, basis);
			for (uint coefficient = 0; coefficient < 9; ++coefficient) probe.irradiance[coefficient].rgb += radiance * basis[coefficient];
			uint face = BakedDirectionFace(direction);
			probe.visibility[face] += float4(distance, distance * distance, 1.0f, backface);
		}
		gBakeProbeOutput[task] = probe;
		return;
	}
	uint outputIndex = stage == 0u ? task : lightmapCount + task;
	float4 accumulated = sampleStart == 0u ? 0.0f.xxxx : gBakeOutput[outputIndex];
	if (stage == 0u && gBakeTexels[task].valid == 0u) {
		gBakeOutput[outputIndex] = 0.0f;
		return;
	}
	for (uint sample = 0; sample < samplesPerTask; ++sample) {
		uint seed = BakeHash(task ^ BakeHash(sampleStart + sample + stage * 0x9277u));
		float3 origin, direction;
		if (stage == 0u) {
			BakedTexel texel = gBakeTexels[task];
			origin = texel.position + texel.normal * normalBias;
			direction = BakeHemisphere(texel.normal, seed);
		} else {
			uint facePixels = captureSize * captureSize;
			uint captureIndex = task / (6u * facePixels);
			uint local = task % (6u * facePixels);
			uint2 pixel = uint2(local % captureSize, (local / captureSize) % captureSize);
			origin = gBakeCaptures[captureIndex].position;
			direction = EnvironmentCubeDirection(local / facePixels, (float2(pixel) + BakeRandom2(seed)) / float(captureSize));
		}
		float distance, backface;
		accumulated.rgb += BakeRadiance(origin, direction, seed, distance, backface);
		accumulated.a += 1.0f;
	}
	gBakeOutput[outputIndex] = accumulated;
}
