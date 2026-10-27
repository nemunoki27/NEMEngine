#ifndef NEM_PARTICLE_GPU_SOURCE
#define NEM_PARTICLE_GPU_SOURCE

#include "../../Mesh/Common/meshShaderSharedTypes.hlsli"
#include "particleSourceGeometry.hlsli"

cbuffer ParticleGPUSourceConstants : register(b0, space2) {
	float4x4 sourceEmitterWorld;
	float4x4 sourceInverseCaptureVP;
	float4x4 sourcePreviousWorld;
	uint sourceKind;
	uint sourceCandidateCount;
	uint sourceVertexCount;
	uint sourceIndexOffset;
	uint sourceVertexOffset;
	uint sourceFlags;
	uint sourceStateSlot;
	uint sourceModuleStateCount;
	float4 sourceTextureSizeOffset;
	float4 sourceMaskRange;
	float4 sourceImageSize;
};

StructuredBuffer<MeshVertex> gGPUSourceVertices : register(t0, space2);
StructuredBuffer<uint> gGPUSourceIndices : register(t1, space2);
StructuredBuffer<uint> gGPUSourceVertexSubMeshes : register(t2, space2);
StructuredBuffer<ParticleSourceSubMeshGPU> gGPUSourceSubMeshes : register(t3, space2);
StructuredBuffer<float> gGPUSourceDistribution : register(t4, space2);
StructuredBuffer<float4> gGPUSourceVertexColors : register(t5, space2);
StructuredBuffer<MeshVertex> gGPUSourcePreviousVertices : register(t6, space2);
Texture2D<float4> gGPUSourceColor : register(t7, space2);
Texture2D<float> gGPUSourceDepth : register(t8, space2);
Texture2D<float4> gGPUSourceMask : register(t9, space2);
Texture2D<float> gGPUSourcePreviousDepth : register(t10, space2);

struct ParticleSourceSample {
	float3 position;
	float3 previousPosition;
	float3 normal;
	float3 previousNormal;
	float4 color;
	float2 uv;
	uint previousValid;
};

bool ParticleSourceInverseBasis(float4x4 matrix, out float3 x, out float3 y, out float3 z, out float inverseDeterminant) {
	x = cross(matrix[1].xyz, matrix[2].xyz);
	y = cross(matrix[2].xyz, matrix[0].xyz);
	z = cross(matrix[0].xyz, matrix[1].xyz);
	float determinant = dot(matrix[0].xyz, x);
	inverseDeterminant = abs(determinant) > 1e-10f && isfinite(determinant) ? rcp(determinant) : 0;
	return inverseDeterminant != 0;
}

bool ParticleSourceInversePoint(float3 samplePoint, float4x4 matrix, out float3 local) {
	float3 x, y, z;
	float inverseDeterminant;
	bool valid = ParticleSourceInverseBasis(matrix, x, y, z, inverseDeterminant);
	float3 offset = samplePoint - matrix[3].xyz;
	local = float3(dot(offset, x), dot(offset, y), dot(offset, z)) * inverseDeterminant;
	return valid && all(isfinite(local));
}

float3 ParticleSourcePreviousNormal(float3 normal, float4x4 matrix, out bool valid) {
	float3 x, y, z;
	float inverseDeterminant;
	valid = ParticleSourceInverseBasis(matrix, x, y, z, inverseDeterminant);
	return ParticleNormalize((normal.x * x + normal.y * y + normal.z * z) * inverseDeterminant, float3(0, 1, 0));
}

// 固定Samplerを増やさず画素中心で同じ色を読む
float4 ParticleSourceImage(Texture2D<float4> image, float2 uv) {
	uint width, height;
	image.GetDimensions(width, height);
	uint2 pixel = min(uint2(saturate(uv) * float2(width, height)), uint2(width, height) - 1u);
	return image.Load(int3(pixel, 0));
}

bool ParticleSourceVertex(uint vertex, out ParticleSourceSample result) {
	result = (ParticleSourceSample) 0;
	if (vertex >= sourceVertexCount) return false;
	uint subMeshIndex = gGPUSourceVertexSubMeshes[vertex];
	ParticleSourceSubMeshGPU subMesh = gGPUSourceSubMeshes[subMeshIndex];
	if (subMesh.enabled == 0) return false;
	MeshVertex current = gGPUSourceVertices[vertex + sourceVertexOffset];
	result.position = mul(current.position, subMesh.world).xyz;
	result.normal = ParticleNormalize(mul(float4(current.normal, 0), subMesh.normal).xyz, float3(0, 1, 0));
	result.uv = current.uv;
	result.color = sourceImageSize.w != 0 && (sourceFlags & 48u) != 0u ? gGPUSourceVertexColors[vertex] : 1.0f.xxxx;
	MeshVertex previous = current;
	if ((sourceFlags & 64u) != 0u) previous = gGPUSourcePreviousVertices[vertex];
	result.previousPosition = mul(previous.position, subMesh.previousWorld).xyz;
	bool previousNormalValid;
	result.previousNormal = ParticleSourcePreviousNormal(previous.normal, subMesh.previousWorld, previousNormalValid);
	result.previousValid = (sourceFlags & 64u) != 0u && sourceImageSize.z > 0 && previousNormalValid ? 1u : 0u;
	return all(isfinite(result.position)) && all(isfinite(result.normal));
}

ParticleSourceSample ParticleSourceBlend(ParticleSourceSample a, ParticleSourceSample b, float t) {
	ParticleSourceSample result;
	result.position = lerp(a.position, b.position, t);
	result.previousPosition = lerp(a.previousPosition, b.previousPosition, t);
	result.normal = ParticleNormalize(lerp(a.normal, b.normal, t), a.normal);
	result.previousNormal = ParticleNormalize(lerp(a.previousNormal, b.previousNormal, t), a.previousNormal);
	result.color = lerp(a.color, b.color, t);
	result.uv = lerp(a.uv, b.uv, t);
	result.previousValid = a.previousValid & b.previousValid;
	return result;
}

bool ParticleSourceGeometry(uint candidate, uint barySeed, out ParticleSourceSample result) {
	result = (ParticleSourceSample) 0;
	if (candidate >= sourceCandidateCount) return false;
	if (sourceKind == 1u) return ParticleSourceVertex(candidate, result);
	uint random = barySeed;
	uint triangleIndex = sourceKind == 2u ? candidate / 3u : candidate;
	uint edge = sourceKind == 2u ? candidate % 3u : 0u;
	uint base = sourceIndexOffset + triangleIndex * 3u;
	ParticleSourceSample a, b, c;
	if (!ParticleSourceVertex(gGPUSourceIndices[base + edge], a) ||
		!ParticleSourceVertex(gGPUSourceIndices[base + (edge + 1u) % 3u], b)) return false;
	if (sourceKind == 2u) {
		result = ParticleSourceBlend(a, b, ParticleRandom(random));
		return true;
	}
	if (!ParticleSourceVertex(gGPUSourceIndices[base + 2u], c)) return false;
	float root = sqrt(ParticleRandom(random));
	float split = ParticleRandom(random);
	result = ParticleSourceBlend(a, ParticleSourceBlend(b, c, split), root);
	result.normal = ParticleNormalize(a.normal * (1 - root) + b.normal * root * (1 - split) + c.normal * root * split, a.normal);
	result.previousNormal = ParticleNormalize(a.previousNormal * (1 - root) + b.previousNormal * root * (1 - split) +
		c.previousNormal * root * split, a.previousNormal);
	return true;
}

bool ParticleSourceUnproject(float2 uv, float depth, float4x4 inverseVP, out float3 position) {
	float4 world = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1), inverseVP);
	position = world.xyz / max(abs(world.w), 1e-8f) * (world.w < 0 ? -1 : 1);
	return depth > 0 && depth < 1 && abs(world.w) > 1e-8f && all(isfinite(position));
}

bool ParticleSourceCamera(float2 uv, out ParticleSourceSample result) {
	result = (ParticleSourceSample) 0;
	if ((sourceFlags & 128u) == 0u) return false;
	uint2 dimensions = uint2(sourceImageSize.xy);
	if (any(dimensions == 0)) return false;
	uint2 pixel = min(uint2(uv * dimensions), dimensions - 1u);
	uv = (float2(pixel) + 0.5f) / dimensions;
	float depth = gGPUSourceDepth.Load(int3(pixel, 0));
	if (!ParticleSourceUnproject(uv, depth, sourceInverseCaptureVP, result.position)) return false;
	uint2 adjacentX = uint2(pixel.x + 1u < dimensions.x ? pixel.x + 1u : max(pixel.x, 1u) - 1u, pixel.y);
	uint2 adjacentY = uint2(pixel.x, pixel.y + 1u < dimensions.y ? pixel.y + 1u : max(pixel.y, 1u) - 1u);
	float3 right, down;
	bool rightValid = ParticleSourceUnproject((float2(adjacentX) + 0.5f) / dimensions,
		gGPUSourceDepth.Load(int3(adjacentX, 0)), sourceInverseCaptureVP, right);
	bool downValid = ParticleSourceUnproject((float2(adjacentY) + 0.5f) / dimensions,
		gGPUSourceDepth.Load(int3(adjacentY, 0)), sourceInverseCaptureVP, down);
	float3 dx = (right - result.position) * (adjacentX.x > pixel.x ? 1 : -1);
	float3 dy = (down - result.position) * (adjacentY.y > pixel.y ? 1 : -1);
	result.normal = rightValid && downValid ? ParticleNormalize(cross(dx, dy), float3(0, 1, 0)) : float3(0, 1, 0);
	result.color = gGPUSourceColor.Load(int3(pixel, 0));
	result.uv = uv;
	result.previousPosition = result.position;
	result.previousNormal = result.normal;
	if ((sourceFlags & 64u) != 0u && sourceImageSize.z > 0) {
		float previousDepth = gGPUSourcePreviousDepth.Load(int3(pixel, 0));
		float3 previous;
		if (ParticleSourceUnproject(uv, previousDepth, sourcePreviousWorld, previous)) {
			float footprint = max(length(dx), length(dy));
			bool sameSurface = abs(previousDepth - depth) < 0.02f && length(previous - result.position) <= max(0.05f, footprint * 8);
			if (sameSurface) {
				result.previousPosition = previous;
				result.previousValid = 1;
			}
		}
	}
	return true;
}

bool ParticleSourceEvaluate(uint candidate, uint sampleSeed, out ParticleSourceSample result) {
	if (sourceKind <= 3u) return ParticleSourceGeometry(candidate, sampleSeed, result);
	uint random = sampleSeed;
	float2 uv;
	if ((sourceFlags & 1u) != 0u) {
		uint2 dimensions = max(uint2(sourceImageSize.xy), 1u.xx);
		uint count = dimensions.x * dimensions.y;
		uint pixel = candidate % count;
		uv = (float2(pixel % dimensions.x, pixel / dimensions.x) + 0.5f) / dimensions;
	} else uv = float2(ParticleRandom(random), ParticleRandom(random));
	if (sourceKind == 5u) return ParticleSourceCamera(uv, result);
	result = (ParticleSourceSample) 0;
	float4 local = float4((uv - 0.5f) * sourceTextureSizeOffset.xy, 0, 1);
	result.position = mul(local, sourceEmitterWorld).xyz;
	result.previousPosition = mul(local, sourcePreviousWorld).xyz;
	float3 x = mul(float4(1, 0, 0, 0), sourceEmitterWorld).xyz;
	float3 y = mul(float4(0, 1, 0, 0), sourceEmitterWorld).xyz;
	result.normal = ParticleNormalize(cross(x, y), float3(0, 0, 1));
	result.previousNormal = ParticleNormalize(cross(sourcePreviousWorld[0].xyz, sourcePreviousWorld[1].xyz), result.normal);
	result.color = ParticleSourceImage(gGPUSourceColor, uv);
	result.uv = uv;
	result.previousValid = (sourceFlags & 64u) != 0u && sourceImageSize.z > 0 ? 1u : 0u;
	return all(isfinite(result.position));
}

uint ParticleSourceSelect(uint claim, inout uint random) {
	if (sourceKind >= 4u) return claim;
	if (sourceCandidateCount == 0) return 0xffffffffu;
	float total = gGPUSourceDistribution[sourceCandidateCount - 1u];
	if (!isfinite(total) || total <= 0) return 0xffffffffu;
	float pick;
	if ((sourceFlags & 1u) == 0u) pick = ParticleRandom(random) * total;
	else if (sourceKind == 1u) pick = (float(claim % max(1u, (uint) total)) + 0.5f);
	else pick = (float(claim % sourceCandidateCount) + 0.5f) / sourceCandidateCount * total;
	uint low = 0, high = sourceCandidateCount;
	while (low < high) {
		uint middle = low + (high - low) / 2u;
		if (gGPUSourceDistribution[middle] <= pick) low = middle + 1u;
		else high = middle;
	}
	return min(low, sourceCandidateCount - 1u);
}

bool ParticleSourceMask(ParticleSourceSample sample) {
	if ((sourceFlags & 8u) == 0u) return true;
	float4 value = (sourceFlags & 32u) != 0u ? sample.color : ParticleSourceImage(gGPUSourceMask, sample.uv);
	uint channel = min((uint) sourceMaskRange.z, 4u);
	float scalar = channel == 4u ? dot(value.rgb, float3(0.2126f, 0.7152f, 0.0722f)) : value[channel];
	return isfinite(scalar) && scalar >= sourceMaskRange.x && scalar <= sourceMaskRange.y;
}

bool ParticleSourceSpawn(inout ParticleAttributesGPU particle, uint claim, uint index, inout uint random) {
	if (sourceKind == 0u) return true;
	uint slot = index * sourceModuleStateCount + sourceStateSlot;
	uint sequence = (sourceFlags & 1u) != 0u ? particle.birthLow : claim;
	for (uint attempt = 0; attempt < 64u; ++attempt) {
		uint candidate = ParticleSourceSelect(sequence + attempt, random);
		uint sampleSeed = ParticleHash(random ^ attempt);
		random = ParticleHash(sampleSeed + 0x9e3779b9u);
		ParticleSourceSample sample;
		if (candidate == 0xffffffffu || !ParticleSourceEvaluate(candidate, sampleSeed, sample) || !ParticleSourceMask(sample)) continue;
		particle.positionAge.xyz = sample.position + sample.normal * sourceTextureSizeOffset.z;
		particle.velocityLifetime.xyz = sample.normal * length(particle.velocityLifetime.xyz);
		if (sample.previousValid != 0 && sourceImageSize.z > 0) {
			float3 currentPosition = sample.position + sample.normal * sourceTextureSizeOffset.z;
			float3 previousPosition = sample.previousPosition + sample.previousNormal * sourceTextureSizeOffset.z;
			particle.velocityLifetime.xyz += (currentPosition - previousPosition) * sourceTextureSizeOffset.w / sourceImageSize.z;
		}
		particle.rotation = ParticleLookRotation(sample.normal);
		gGPUModuleStates[slot] = float4(asfloat(candidate), asfloat(sampleSeed), sourceMaskRange.w,
			asfloat(sourceKind | (sourceFlags & 0xffff0000u)));
		gGPUModuleStates[slot + 1u] = sample.color;
		return true;
	}
	particle.alive = 0;
	gGPUModuleStates[slot] = float4(asfloat(0xffffffffu), 0, 0, 0);
	return false;
}

void ParticleSourceAttach(inout ParticleAttributesGPU particle, uint index) {
	if (sourceKind == 0u || (sourceFlags & 2u) == 0u) return;
	uint slot = index * sourceModuleStateCount + sourceStateSlot;
	float4 state = gGPUModuleStates[slot];
	if (asuint(state.x) == 0xffffffffu || (asuint(state.w) & 255u) != sourceKind) return;
	if ((asuint(state.w) & 0xffff0000u) != (sourceFlags & 0xffff0000u)) {
		state.x = asfloat(0xffffffffu);
		state.w = asfloat(asuint(state.w) | 512u);
		gGPUModuleStates[slot] = state;
		return;
	}
	if (asuint(state.z) == asuint(sourceMaskRange.w)) return;
	ParticleSourceSample sample;
	if (!ParticleSourceEvaluate(asuint(state.x), asuint(state.y), sample) || sample.previousValid == 0) return;
	float3 currentPosition = sample.position + sample.normal * sourceTextureSizeOffset.z;
	float3 previousPosition = sample.previousPosition + sample.previousNormal * sourceTextureSizeOffset.z;
	float3 delta = currentPosition - previousPosition;
	if (particle.hasParent != 0u) {
		ParticleParentGPU parent = gGPUParents[particle.parentPhase];
		if (parent.previousValid != 0u) {
			float3 previousLocal;
			if (!ParticleSourceInversePoint(previousPosition, parent.previousMatrix, previousLocal)) return;
			delta = mul(float4(currentPosition, 1), parent.inverse).xyz - previousLocal;
		} else delta = mul(float4(delta, 0), parent.inverse).xyz;
	}
	particle.positionAge.xyz += delta;
	state.z = sourceMaskRange.w;
	gGPUModuleStates[slot] = state;
}

void ParticleSourceApplyColor(inout ParticleAttributesGPU particle, uint index) {
	if (sourceKind == 0u || (sourceFlags & 4u) == 0u) return;
	uint slot = index * sourceModuleStateCount + sourceStateSlot;
	float4 state = gGPUModuleStates[slot];
	if (asuint(state.x) == 0xffffffffu && (asuint(state.w) & 512u) == 0u) return;
	ParticleGPUPhase phase = gGPUPhases[particle.phase];
	bool writesColor = false;
	for (uint instruction = 0; instruction < phase.instructionCount; ++instruction) {
		if (gGPUInstructions[phase.instructionOffset + instruction].operation == 0u) {
			writesColor = true;
			break;
		}
	}
	if (writesColor || (asuint(state.w) & 256u) == 0u) {
		float4 inherited = gGPUModuleStates[slot + 1u];
		particle.color *= inherited;
		for (uint instruction = 0; instruction < phase.instructionCount; ++instruction) {
			ParticleGPUInstruction module = gGPUInstructions[phase.instructionOffset + instruction];
			if (module.operation == 3u && (module.flags & 1u) != 0u) particle.emissive.xyz *= inherited.rgb;
		}
	}
	state.w = asfloat(asuint(state.w) | 256u);
	gGPUModuleStates[slot] = state;
}

#endif
