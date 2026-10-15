#ifndef NEM_BAKED_LIGHTING_RUNTIME_HLSLI
#define NEM_BAKED_LIGHTING_RUNTIME_HLSLI

#include "bakedLightingTypes.hlsli"

cbuffer BakedLightingConstants : register(b3) {

	uint bakedTriangleCount;
	uint bakedAtlasSize;
	uint bakedProbeCount;
	uint bakedCaptureCount;
	float3 bakedProbeMinimum;
	float bakedProbeSpacing;
	uint3 bakedProbeDimensions;
	float bakedSurfaceTolerance;
	uint bakedReceiverCount;
	uint3 _bakedPad;
};
StructuredBuffer<BakedTriangle> gBakedTriangles : register(t20);
StructuredBuffer<BakedNode> gBakedNodes : register(t21);
StructuredBuffer<uint> gBakedOrder : register(t22);
StructuredBuffer<float4> gBakedLightmaps : register(t23);
StructuredBuffer<BakedProbe> gBakedProbes : register(t24);
StructuredBuffer<BakedCapture> gBakedCaptures : register(t25);
StructuredBuffer<float4> gBakedReflectionPixels : register(t26);
StructuredBuffer<uint> gBakedReceiverEntities : register(t27);

float3 BakedTriangleWeights(float3 position, BakedTriangle geometry, out float distance) {

	float3 first = geometry.vertices[0].position;
	float3 ab = geometry.vertices[1].position - first;
	float3 ac = geometry.vertices[2].position - first;
	float3 geometricNormal = normalize(cross(ab, ac));
	distance = abs(dot(position - first, geometricNormal));
	float3 offset = position - first;
	float abDot = dot(ab, ab);
	float acDot = dot(ac, ac);
	float crossDot = dot(ab, ac);
	float determinant = abDot * acDot - crossDot * crossDot;
	if (determinant <= 1e-12f) { distance = 1e30f; return -1.0f.xxx; }
	float second = (acDot * dot(offset, ab) - crossDot * dot(offset, ac)) / determinant;
	float third = (abDot * dot(offset, ac) - crossDot * dot(offset, ab)) / determinant;
	return float3(1.0f - second - third, second, third);
}

bool BakedReadLightmap(uint atlas, float2 uv, out float3 irradiance) {

	float2 texel = uv * float(bakedAtlasSize) - 0.5f;
	int2 base = int2(floor(texel));
	float2 blend = frac(texel);
	float4 sum = 0.0f;
	float totalWeight = 0.0f;
	for (uint y = 0; y < 2u; ++y) {
		for (uint x = 0; x < 2u; ++x) {
			int2 pixel = clamp(base + int2(x, y), 0, int(bakedAtlasSize) - 1);
			float4 sample = gBakedLightmaps[(atlas * bakedAtlasSize + pixel.y) * bakedAtlasSize + pixel.x];
			float weight = (x == 0u ? 1.0f - blend.x : blend.x) * (y == 0u ? 1.0f - blend.y : blend.y);
			weight *= sample.a > 0.0f ? 1.0f : 0.0f;
			sum += sample * weight;
			totalWeight += weight;
		}
	}
	irradiance = sum.rgb / max(totalWeight, 1e-6f);
	return totalWeight > 1e-6f;
}

// 描画LODの位置から別保存したLightmapのUVを復元する
bool SampleBakedLightmap(float3 position, float3 normal, uint receiverEntity, out float3 irradiance) {

	irradiance = 0.0f;
	if (bakedTriangleCount == 0u) return false;
	uint stack[48];
	uint stackCount = 1u;
	stack[0] = 0u;
	float closestDistance = bakedSurfaceTolerance;
	uint closestTriangle = 0xFFFFFFFFu;
	float3 closestWeights = 0.0f;
	while (stackCount > 0u) {
		BakedNode node = gBakedNodes[stack[--stackCount]];
		float3 delta = max(max(node.minimum - position, position - node.maximum), 0.0f);
		if (dot(delta, delta) > closestDistance * closestDistance) continue;
		if ((node.left & 0x80000000u) == 0u) {
			if (stackCount + 2u > 48u) continue;
			stack[stackCount++] = node.left;
			stack[stackCount++] = node.right;
			continue;
		}
		for (uint index = 0; index < node.right; ++index) {
			uint triangleIndex = gBakedOrder[(node.left & 0x7FFFFFFFu) + index];
			BakedTriangle geometry = gBakedTriangles[triangleIndex];
			if (geometry.receiverIndex >= bakedReceiverCount || gBakedReceiverEntities[geometry.receiverIndex] != receiverEntity) continue;
			float distance;
			float3 weights = BakedTriangleWeights(position, geometry, distance);
			float3 surfaceNormal = normalize(geometry.vertices[0].normal * weights.x + geometry.vertices[1].normal * weights.y + geometry.vertices[2].normal * weights.z);
			if (distance > closestDistance || min(weights.x, min(weights.y, weights.z)) < -0.001f || dot(surfaceNormal, normal) < 0.2f) continue;
			closestDistance = distance;
			closestTriangle = triangleIndex;
			closestWeights = weights;
		}
	}
	if (closestTriangle == 0xFFFFFFFFu) return false;
	BakedTriangle geometry = gBakedTriangles[closestTriangle];
	float2 uv = float2(geometry.vertices[0].lightmapU, geometry.vertices[0].lightmapV) * closestWeights.x +
		float2(geometry.vertices[1].lightmapU, geometry.vertices[1].lightmapV) * closestWeights.y +
		float2(geometry.vertices[2].lightmapU, geometry.vertices[2].lightmapV) * closestWeights.z;
	return BakedReadLightmap(geometry.atlasIndex, uv, irradiance);
}

// 壁を越えるProbeの補間を距離分布で抑える
bool SampleBakedProbes(float3 position, float3 normal, out float3 irradiance) {

	irradiance = 0.0f;
	if (bakedProbeCount == 0u) return false;
	float3 grid = (position - bakedProbeMinimum) / bakedProbeSpacing;
	if (any(grid < 0.0f) || any(grid > float3(bakedProbeDimensions - 1u))) return false;
	int3 base = clamp(int3(floor(grid)), 0, int3(bakedProbeDimensions) - 2);
	float3 blend = saturate(grid - float3(base));
	float basis[9];
	BakedSHBasis(normal, basis);
	float totalWeight = 0.0f;
	for (uint corner = 0; corner < 8u; ++corner) {
		uint3 offset = uint3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
		uint3 cell = uint3(base) + offset;
		BakedProbe probe = gBakedProbes[(cell.z * bakedProbeDimensions.y + cell.y) * bakedProbeDimensions.x + cell.x];
		if (probe.radius <= 0.0f) continue;
		float3 trilinear = lerp(1.0f - blend, blend, float3(offset));
		float weight = trilinear.x * trilinear.y * trilinear.z;
		float3 delta = position + normal * bakedSurfaceTolerance - probe.position;
		float distance = length(delta);
		float3 direction = delta / max(distance, 1e-6f);
		float4 visibility = probe.visibility[BakedDirectionFace(direction)];
		float beyondMean = max(distance - visibility.x, 0.0f);
		float variance = max(visibility.y, bakedSurfaceTolerance * bakedSurfaceTolerance);
		float visible = beyondMean == 0.0f ? 1.0f : variance / (variance + beyondMean * beyondMean);
		weight *= smoothstep(0.05f, 1.0f, visible);
		weight *= max(0.2f, dot(normal, -direction) * 0.5f + 0.5f);
		float3 color = 0.0f;
		for (uint coefficient = 0; coefficient < 9u; ++coefficient) color += probe.irradiance[coefficient].rgb * basis[coefficient];
		irradiance += max(color, 0.0f) * weight;
		totalWeight += weight;
	}
	if (totalWeight <= 1e-5f) return false;
	irradiance /= totalWeight;
	return true;
}

bool SampleBakedIndirect(float3 position, float3 normal, uint flags, uint receiverEntity, out float3 irradiance) {

	irradiance = 0.0f;
	if ((flags & (1u << 6u)) != 0u && SampleBakedLightmap(position, normal, receiverEntity, irradiance)) return true;
	return (flags & ((1u << 6u) | (1u << 7u))) != 0u && SampleBakedProbes(position, normal, irradiance);
}

float3 BakedReadCapture(BakedCapture capture, float3 direction, uint mip) {

	uint offset = capture.dataOffset;
	uint size = capture.size;
	for (uint index = 0; index < mip; ++index) { offset += 6u * size * size; size = max(size >> 1u, 1u); }
	uint face = BakedDirectionFace(direction);
	float2 st;
	if (face == 0u) st = float2(-direction.z, direction.y) / abs(direction.x);
	else if (face == 1u) st = float2(direction.z, direction.y) / abs(direction.x);
	else if (face == 2u) st = float2(direction.x, -direction.z) / abs(direction.y);
	else if (face == 3u) st = float2(direction.x, direction.z) / abs(direction.y);
	else if (face == 4u) st = float2(direction.x, direction.y) / abs(direction.z);
	else st = float2(-direction.x, direction.y) / abs(direction.z);
	float2 pixel = (st * float2(0.5f, -0.5f) + 0.5f) * float(size) - 0.5f;
	int2 base = int2(floor(pixel));
	float2 blend = frac(pixel);
	float3 color = 0.0f;
	for (uint y = 0; y < 2u; ++y) {
		for (uint x = 0; x < 2u; ++x) {
			int2 coordinate = clamp(base + int2(x, y), 0, int(size) - 1);
			float weight = (x == 0u ? 1.0f - blend.x : blend.x) * (y == 0u ? 1.0f - blend.y : blend.y);
			color += gBakedReflectionPixels[offset + (face * size + coordinate.y) * size + coordinate.x].rgb * weight;
		}
	}
	return color;
}

// 近いCaptureを補間し範囲外ではSkyboxへ渡す
bool SampleBakedReflection(float3 position, float3 direction, float roughness, out float3 radiance, out float coverageAmount) {

	radiance = 0.0f;
	coverageAmount = 0.0f;
	float totalWeight = 0.0f;
	for (uint index = 0; index < bakedCaptureCount; ++index) {
		BakedCapture capture = gBakedCaptures[index];
		float3 offset = position - capture.position;
		float3 localPosition = mul(float4(position, 1.0f), capture.worldToBox).xyz;
		float coverage = capture.shape == 0u ? length(offset) / capture.radius : max(abs(localPosition.x / capture.extents.x),
			max(abs(localPosition.y / capture.extents.y), abs(localPosition.z / capture.extents.z)));
		if (coverage >= 1.0f) continue;
		float3 corrected = direction;
		if (capture.shape == 1u) {
			float3 localDirection = mul(float4(direction, 0.0f), capture.worldToBox).xyz;
			float3 safeDirection = sign(localDirection + 1e-10f) * max(abs(localDirection), 1e-6f);
			float3 distance = (sign(safeDirection) * capture.extents - localPosition) / safeDirection;
			corrected = normalize(offset + direction * min(distance.x, min(distance.y, distance.z)));
		} else {
			float projection = dot(offset, direction);
			float distance = -projection + sqrt(max(projection * projection + capture.radius * capture.radius - dot(offset, offset), 0.0f));
			corrected = normalize(offset + direction * distance);
		}
		float mip = saturate(roughness) * float(max(capture.mipCount, 1u) - 1u);
		uint lower = uint(floor(mip));
		float3 color = lerp(BakedReadCapture(capture, corrected, lower),
			BakedReadCapture(capture, corrected, min(lower + 1u, capture.mipCount - 1u)), frac(mip));
		float fade = smoothstep(0.0f, 0.2f, 1.0f - coverage);
		coverageAmount = max(coverageAmount, fade);
		float weight = fade / max(capture.radius * capture.radius, 1.0f);
		radiance += color * capture.intensity * weight;
		totalWeight += weight;
	}
	if (totalWeight <= 1e-8f) return false;
	radiance /= totalWeight;
	return true;
}

#endif
