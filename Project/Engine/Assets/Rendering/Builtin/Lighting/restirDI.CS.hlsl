//============================================================================
//	include
//============================================================================
#include "../Mesh/Common/deferredGBuffer.hlsli"
#include "lightShaderData.hlsli"
#include "lightAttenuation.hlsli"
#include "rectAreaLight.hlsli"
#include "restirDIReservoir.hlsli"

Texture2D<float4> gAlbedo : register(t0);
Texture2D<float4> gNormal : register(t1);
Texture2D<float4> gWorldPos : register(t2);
Texture2D<float4> gMaterial : register(t3);
Texture2D<float2> gMotion : register(t4);
Texture2D<uint> gFlags : register(t5);
StructuredBuffer<ReSTIRReservoir> gReSTIRInput : register(t21);
StructuredBuffer<uint> gReSTIRLightRemap : register(t22);
StructuredBuffer<uint2> gReSTIRRaw : register(t23);
StructuredBuffer<uint2> gReSTIRPrevious : register(t24);
RWStructuredBuffer<uint2> gReSTIRRadianceOut : register(u1);
RWStructuredBuffer<ReSTIRReservoir> gReSTIROutput : register(u0);

cbuffer ReSTIRParameters : register(b1) {
	uint2 viewportSize;
	uint frameIndex;
	uint resetHistory;
	float3 cameraPos;
	uint remapLights;
	float4x4 viewMatrix;
	float shadowNormalBias;
	float shadowMaxDistance;
	uint softShadowSampleCount;
	uint radianceHistoryLimit;
};
#include "inlineShadowCommon.hlsli"
#define NEM_RESTIR_SHADE
#include "localDirectLighting.hlsli"
#include "worldLightIndex.hlsli"

struct Receiver {
	float3 V;
	float3 albedo;
	float metallic;
	float roughness;
	float3 F0;
};

uint PackMaterial(float3 albedo, float metallic, float roughness) {
	uint3 rgb = uint3(round(saturate(albedo) * 63.0f));
	return rgb.x | (rgb.y << 6) | (rgb.z << 12) |
		(uint(round(metallic * 63.0f)) << 18) | (uint(round(roughness * 63.0f)) << 24);
}

Receiver LoadReceiver(uint2 pixel, float3 position) {
	Receiver receiver;
	receiver.V = normalize(cameraPos - position);
	receiver.albedo = gAlbedo.Load(int3(pixel, 0)).rgb;
	float2 material = gMaterial.Load(int3(pixel, 0)).xy;
	receiver.metallic = material.x;
	receiver.roughness = max(0.04f, material.y);
	receiver.F0 = lerp(0.04f.xxx, receiver.albedo, receiver.metallic);
	return receiver;
}

struct DILightList {
	uint count;
	uint offset;
	uint worldList;
	bool clustered;
};

DILightList ResolveDILights(uint2 pixel, float3 position) {
	LightClusterHeader header;
	DILightList list;
	list.clustered = ResolveLightCluster(pixel, position, header);
	list.count = list.clustered ? header.count : localCount;
	list.offset = list.clustered ? header.offset : 0u;
	list.worldList = 0u;
	if (localCount > 64u) {
		WorldLightCursor world = BeginWorldLights(position);
		if (world.list != 0u && world.end - world.current < list.count) {
			list.count = world.end - world.current;
			list.offset = world.current;
			list.worldList = world.list;
		}
	}
	return list;
}

uint DILightAt(DILightList list, uint slot) {
	if (list.worldList != 0u) return WorldCellLightAt(list.worldList, list.offset + slot);
	return list.clustered ? gLightClusterIndices[list.offset + slot] : slot;
}

float3 ShadeDirect(DILightList list, ReSTIRReservoir surface, Receiver receiver, uint2 pixel) {
	float3 radiance = 0;
	float3 normal = ReceiverNormal(surface);
	[loop]
	for (uint slot = 0u; slot < list.count; ++slot) {
		uint index = DILightAt(list, slot);
		radiance += EvaluateLocalLight(index, surface.position, normal, receiver.V,
			receiver.albedo, receiver.metallic, receiver.roughness, receiver.F0,
			surface.flags, softShadowSampleCount != 0u, pixel);
	}
	return radiance;
}

void SetReceiver(uint2 pixel, Receiver receiver, inout ReSTIRReservoir result) {
	float3 normal = normalize(gNormal.Load(int3(pixel, 0)).xyz * 2.0f - 1.0f);
	result.normalXY = normal.xy;
	result.material = PackMaterial(receiver.albedo, receiver.metallic, receiver.roughness);
	if (normal.z < 0.0f) result.material |= 1u << 30u;
	uint3 rgb = asuint(receiver.albedo);
	uint signature = (rgb.x ^ (rgb.y * 16777619u)) * 2246822519u;
	signature = (signature ^ rgb.z) * 3266489917u;
	result.surfaceSignature = (signature ^ asuint(receiver.metallic)) * 16777619u ^ asuint(receiver.roughness);
}

// PCGの出力置換で近接画素と連続フレームの乱数を分離する
float NextRandom(inout uint state) {
	state = state * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return float(((word >> 22u) ^ word) >> 8u) * (1.0f / 16777216.0f);
}

float Target(uint index, ReSTIRReservoir surface, Receiver receiver, uint2 pixel) {
	float3 radiance = EvaluateLocalLight(index, surface.position, ReceiverNormal(surface), receiver.V,
		receiver.albedo, receiver.metallic, receiver.roughness, receiver.F0, surface.flags, false, pixel);
	return max(dot(radiance, float3(0.2126f, 0.7152f, 0.0722f)), 0.0f);
}

// 可視性の近似を候補の重みに入れ、半影に寄与する光源も候補に残す
float VisibilityWeight(uint index, ReSTIRReservoir surface) {
	if (softShadowSampleCount == 0u || (surface.flags & kMaterialFlagReceiveShadow) == 0u) return 1.0f;
	float3 lightPos;
	float strength, extent;
	uint filter;
	if (index < pointCount) {
		PointLight light = gPointLights[index];
		lightPos = light.pos;
		strength = light.shadowStrength;
		extent = light.shadowRadius;
		filter = light.shadowFilterIndex;
	} else if (index < pointCount + spotCount) {
		SpotLight light = gSpotLights[index - pointCount];
		lightPos = light.pos;
		strength = light.shadowStrength;
		extent = light.shadowRadius;
		filter = light.shadowFilterIndex;
	} else {
		RectLight light = gRectLights[index - pointCount - spotCount];
		lightPos = light.pos;
		strength = light.shadowStrength;
		extent = max(light.sourceWidth, light.sourceHeight);
		filter = light.shadowFilterIndex;
	}
	if (strength <= 0.0f) return 1.0f;
	float3 origin = surface.position + ReceiverNormal(surface) * shadowNormalBias;
	float3 toLight = lightPos - origin;
	float distance = length(toLight);
	if (distance <= 0.001f) return 1.0f;
	float floorWeight = extent > 0.001f ? 0.20f : 0.04f;
	return TraceShadowRay(origin, toLight / distance, distance, filter) ? max(floorWeight, 1.0f - strength) : 1.0f;
}

void Stream(inout ReSTIRReservoir result, uint index, float target, float weight, uint count,
	inout float weightSum, inout uint random) {
	result.sampleCount += count;
	if (!(weight > 0.0f) || !isfinite(weight)) return;
	weightSum += weight;
	if (NextRandom(random) * weightSum < weight) {
		result.lightIndex = index;
		result.target = target;
	}
}

bool Compatible(ReSTIRReservoir current, ReSTIRReservoir other, bool temporal) {
	if (current.flags != other.flags || dot(ReceiverNormal(current), ReceiverNormal(other)) < 0.95f) return false;
	[unroll]
	for (uint shift = 0u; shift < 30u; shift += 6u) {
		int a = int((current.material >> shift) & 63u);
		int b = int((other.material >> shift) & 63u);
		if (abs(a - b) > 3) return false;
	}
	float footprint = max(0.005f, length(cameraPos - current.position) / float(viewportSize.y));
	float3 offset = other.position - current.position;
	return abs(dot(ReceiverNormal(current), offset)) <= footprint * 2.0f &&
		dot(offset, offset) <= footprint * footprint * (temporal ? 16.0f : 144.0f);
}

// 上位2ビットには、再投影先の4画素のうち採用した位置を記録する
uint SampleCount(ReSTIRReservoir reservoir) {
	return reservoir.sampleCount & 0x3fffffffu;
}

bool ReprojectReceiver(uint2 pixel, ReSTIRReservoir surface, out uint corner, out ReSTIRReservoir previous) {
	float2 q = float2(pixel) - gMotion.Load(int3(pixel, 0)) * viewportSize;
	previous = (ReSTIRReservoir)0;
	corner = 0u;
	if (!all(isfinite(q)) || any(q <= -1.0f) || any(q >= float2(viewportSize))) return false;
	int2 base = int2(floor(q));
	float2 fraction = frac(q);
	uint nearest = (fraction.x >= 0.5f ? 1u : 0u) | (fraction.y >= 0.5f ? 2u : 0u);
	uint second = abs(fraction.x - 0.5f) < abs(fraction.y - 0.5f) ? 1u : 2u;
	[loop]
	for (uint i = 0u; i < 4u; ++i) {
		uint choice = nearest ^ (i == 0u ? 0u : i == 1u ? second : i == 2u ? (3u ^ second) : 3u);
		int2 offset = int2(choice & 1u, choice >> 1u);
		int2 candidate = base + offset;
		if (any(candidate < 0) || any(candidate >= int2(viewportSize))) continue;
		if (any(lerp(1.0f - fraction, fraction, float2(offset)) <= 0.0f)) continue;
		ReSTIRReservoir other = gReSTIRInput[uint(candidate.y) * viewportSize.x + uint(candidate.x)];
		if (Compatible(surface, other, true)) {
			previous = other;
			corner = choice;
			return true;
		}
	}
	return false;
}

bool CanIlluminate(uint index, float3 position, float3 normal, uint flags) {
	float3 lightPos;
	float radius;
	uint layers;
	if (index < pointCount) {
		PointLight light = gPointLights[index];
		lightPos = light.pos;
		radius = light.radius;
		layers = light.renderingLayerMask;
	} else if (index < pointCount + spotCount) {
		SpotLight light = gSpotLights[index - pointCount];
		lightPos = light.pos;
		radius = light.distance;
		layers = light.renderingLayerMask;
	} else {
		RectLight light = gRectLights[index - pointCount - spotCount];
		lightPos = light.pos;
		radius = light.attenuationRadius;
		layers = light.renderingLayerMask;
	}
	float3 delta = lightPos - position;
	if (!IsLightRenderingLayerEnabled(layers, flags >> 8u) || radius <= 0.0001f || length(delta) >= radius) return false;
	return index >= pointCount + spotCount || dot(delta, normal) > 0.0f;
}

void Merge(inout ReSTIRReservoir result, ReSTIRReservoir source, Receiver receiver, uint2 pixel,
	uint limit, inout float sum, inout uint random) {
	if (source.weight <= 0.0f || source.lightIndex >= localCount) return;
	uint count = min(SampleCount(source), limit);
	float target = Target(source.lightIndex, result, receiver, pixel);
	if (target > 0.0f) target *= VisibilityWeight(source.lightIndex, result);
	Stream(result, source.lightIndex, target, target * source.weight * count, count, sum, random);
}

void Store(uint address, ReSTIRReservoir result, float sum) {
	if (result.target > 0.0f && SampleCount(result) != 0u)
		result.weight = (sum / result.target) / SampleCount(result);
	else result.weight = 0.0f;
	gReSTIROutput[address] = result;
}

[numthreads(8, 8, 1)]
void initialTemporal(uint3 thread : SV_DispatchThreadID) {
	uint2 pixel = thread.xy;
	if (any(pixel >= viewportSize)) return;
	uint address = pixel.y * viewportSize.x + pixel.x;
	ReSTIRReservoir result = (ReSTIRReservoir)0;
	result.flags = gFlags.Load(int3(pixel, 0));
	if ((result.flags & (kMaterialFlagSurface | kMaterialFlagLighting)) != (kMaterialFlagSurface | kMaterialFlagLighting)) {
		gReSTIROutput[address] = result;
		return;
	}
	result.position = gWorldPos.Load(int3(pixel, 0)).xyz;
	Receiver receiver = LoadReceiver(pixel, result.position);
	SetReceiver(pixel, receiver, result);

	DILightList lights = ResolveDILights(pixel, result.position);
	// ワールドセルやクラスタが大きい場合も、受光点に届く灯数で判定する
	uint eligible = lights.count;
	if (eligible > 64u) {
		eligible = 0u;
		float3 normal = ReceiverNormal(result);
		[loop]
		for (uint i = 0u; i < lights.count && eligible <= 64u; ++i) {
			if (CanIlluminate(DILightAt(lights, i), result.position, normal, result.flags)) ++eligible;
		}
	}
	if (eligible <= 64u) {
		result.sampleCount = 1u;
		result.lightIndex = 0xffffffffu;
		gReSTIROutput[address] = result;
		return;
	}
	float sum = 0.0f;
	uint random = address + frameIndex * 0x9e3779b9u;
	const uint candidates = 8u;
	// 密集している受光点の候補から等間隔に異なる灯を抽出する
	float offset = NextRandom(random);
	[loop]
	for (uint i = 0; i < candidates; ++i) {
		uint slot = min(uint((float(i) + offset) * (float(lights.count) / candidates)), lights.count - 1u);
		uint index = DILightAt(lights, slot);
		float target = Target(index, result, receiver, pixel);
		Stream(result, index, target, target * lights.count, 1u, sum, random);
	}
	// 初期分布から可視性を含む分布へ変換する
	if (result.target > 0.0f) {
		float visibility = VisibilityWeight(result.lightIndex, result);
		result.target *= visibility;
		sum *= visibility;
	}
	if (resetHistory == 0u) {
		ReSTIRReservoir other;
		uint corner;
		if (ReprojectReceiver(pixel, result, corner, other)) {
			result.material |= 1u << 31u;
			result.sampleCount |= corner << 30u;
			if (other.weight > 0.0f) {
				if (remapLights != 0u) other.lightIndex = gReSTIRLightRemap[other.lightIndex];
				Merge(result, other, receiver, pixel, 16u, sum, random);
			}
		}
	}
	Store(address, result, sum);
}

[numthreads(8, 8, 1)]
void spatial(uint3 thread : SV_DispatchThreadID) {
	uint2 pixel = thread.xy;
	if (any(pixel >= viewportSize)) return;
	uint address = pixel.y * viewportSize.x + pixel.x;
	ReSTIRReservoir result = gReSTIRInput[address];
	if (SampleCount(result) <= 1u) {
		gReSTIROutput[address] = result;
		return;
	}
	Receiver receiver = LoadReceiver(pixel, result.position);
	float sum = result.weight * result.target * SampleCount(result);
	uint random = address + frameIndex * 0x9e3779b9u + 0x2e399a3bu;
	[unroll]
	for (uint i = 0; i < 3; ++i) {
		int2 offset;
		offset.x = int(NextRandom(random) * 9.0f) - 4;
		offset.y = int(NextRandom(random) * 9.0f) - 4;
		int2 neighbor = int2(pixel) + offset;
		if (all(offset == 0) || any(neighbor < 0) || any(neighbor >= int2(viewportSize))) continue;
		ReSTIRReservoir other = gReSTIRInput[neighbor.y * viewportSize.x + neighbor.x];
		if (Compatible(result, other, false)) Merge(result, other, receiver, pixel, 8u, sum, random);
	}
	Store(address, result, sum);
}

[numthreads(8, 8, 1)]
void shade(uint3 thread : SV_DispatchThreadID) {
	uint2 pixel = thread.xy;
	if (any(pixel >= viewportSize)) return;
	uint address = pixel.y * viewportSize.x + pixel.x;
	if (radianceHistoryLimit == 0xfffffffeu) {
		ReSTIRReservoir surface = (ReSTIRReservoir)0;
		surface.flags = gFlags.Load(int3(pixel, 0));
		float3 radiance = 0;
		if ((surface.flags & (kMaterialFlagSurface | kMaterialFlagLighting)) == (kMaterialFlagSurface | kMaterialFlagLighting)) {
			surface.position = gWorldPos.Load(int3(pixel, 0)).xyz;
			Receiver receiver = LoadReceiver(pixel, surface.position);
			SetReceiver(pixel, receiver, surface);
			radiance = ShadeDirect(ResolveDILights(pixel, surface.position), surface, receiver, pixel);
		}
		gReSTIRRadianceOut[address] = PackReSTIRRadiance(radiance, 1.0f);
		return;
	}
	ReSTIRReservoir selected = gReSTIRInput[address];
	float3 radiance = 0.0f.xxx;
	if (SampleCount(selected) == 1u && selected.weight == 0.0f) {
		Receiver surface = LoadReceiver(pixel, selected.position);
		radiance = ShadeDirect(ResolveDILights(pixel, selected.position), selected, surface, pixel);
	} else if (selected.weight > 0.0f && selected.lightIndex < localCount) {
		Receiver surface = LoadReceiver(pixel, selected.position);
		radiance = EvaluateLocalLight(selected.lightIndex, selected.position, ReceiverNormal(selected), surface.V,
			surface.albedo, surface.metallic, surface.roughness, surface.F0,
			selected.flags, softShadowSampleCount != 0u, pixel) * selected.weight;
	}
	gReSTIRRadianceOut[address] = PackReSTIRRadiance(radiance, 1.0f);
}

bool SameReceiver(ReSTIRReservoir stored, ReSTIRReservoir surface) {
	return stored.flags == surface.flags && all(stored.position == surface.position) &&
		all(stored.normalXY == surface.normalXY) && stored.surfaceSignature == surface.surfaceSignature &&
		(stored.material & 0x7fffffffu) == surface.material;
}

void ResolveStable(uint2 pixel, uint address, bool exact, bool reuse) {
	ReSTIRReservoir surface = (ReSTIRReservoir)0;
	surface.flags = gFlags.Load(int3(pixel, 0));
	if ((surface.flags & (kMaterialFlagSurface | kMaterialFlagLighting)) != (kMaterialFlagSurface | kMaterialFlagLighting)) {
		if (exact) gReSTIROutput[address] = surface;
		gReSTIRRadianceOut[address] = 0u.xx;
		return;
	}
	surface.position = gWorldPos.Load(int3(pixel, 0)).xyz;
	Receiver receiver = LoadReceiver(pixel, surface.position);
	SetReceiver(pixel, receiver, surface);
	if (exact) {
		if (reuse && SameReceiver(gReSTIROutput[address], surface)) return;
		gReSTIROutput[address] = surface;
		gReSTIRRadianceOut[address] = PackReSTIRRadiance(
			ShadeDirect(ResolveDILights(pixel, surface.position), surface, receiver, pixel), 1.0f);
		return;
	}
	bool same = SameReceiver(gReSTIRInput[address], surface);
	uint2 previous = gReSTIRPrevious[address];
	uint stratum = (pixel.x & 3u) | ((pixel.y & 3u) << 2u);
	float confidence = UnpackReSTIRRadiance(previous).a;
	if (same && confidence > 0.0f && (confidence == 64.0f || stratum != (frameIndex & 15u))) {
		gReSTIRRadianceOut[address] = previous;
		return;
	}
	float3 total = ShadeDirect(ResolveDILights(pixel, surface.position), surface, receiver, pixel);
	gReSTIRRadianceOut[address] = PackReSTIRRadiance(total, same ? 64.0f : 0.0f);
}

// 8×8画素と周囲2画素を共有し、同じ受光面を近傍ごとに読み直さない
groupshared ReSTIRReservoir filterSurface[144];
groupshared uint2 filterRadiance[144];

[numthreads(8, 8, 1)]
void denoise(uint3 thread : SV_DispatchThreadID, uint3 local : SV_GroupThreadID,
	uint groupIndex : SV_GroupIndex) {
	uint2 pixel = thread.xy;
	uint address = pixel.y * viewportSize.x + pixel.x;
	if (radianceHistoryLimit >= 0xfffffffcu) {
		if (all(pixel < viewportSize)) ResolveStable(pixel, address,
			radianceHistoryLimit != 0xffffffffu, radianceHistoryLimit == 0xfffffffcu);
		return;
	}
	int2 origin = int2(pixel - local.xy) - 2;
	for (uint slot = groupIndex; slot < 144u; slot += 64u) {
		int2 q = clamp(origin + int2(slot % 12u, slot / 12u), 0, int2(viewportSize) - 1);
		uint source = uint(q.y) * viewportSize.x + uint(q.x);
		filterSurface[slot] = gReSTIRInput[source];
		filterRadiance[slot] = gReSTIRRaw[source];
	}
	GroupMemoryBarrierWithGroupSync();
	if (any(pixel >= viewportSize)) return;
	int centerSlot = int(local.y + 2u) * 12 + int(local.x + 2u);
	uint2 raw = filterRadiance[centerSlot];
	ReSTIRReservoir center = filterSurface[centerSlot];
	if (SampleCount(center) <= 1u) {
		gReSTIRRadianceOut[address] = raw;
		return;
	}
	float3 current = UnpackReSTIRRadiance(raw).rgb;
	float3 lower = current, upper = current;
	float3 sum = current;
	float total = 1.0f;
	float roughness = float((center.material >> 24u) & 63u) / 63.0f;
	[unroll]
	for (int y = -2; y <= 2; ++y) {
		[unroll]
		for (int x = -2; x <= 2; ++x) {
			int2 q = int2(pixel) + int2(x, y);
			if ((x == 0 && y == 0) || any(q < 0) || any(q >= int2(viewportSize))) continue;
			int slot = centerSlot + y * 12 + x;
			ReSTIRReservoir other = filterSurface[slot];
			if (!Compatible(center, other, false) || SampleCount(other) == 0u) continue;
			float3 color = UnpackReSTIRRadiance(filterRadiance[slot]).rgb;
			float agreement = saturate(dot(ReceiverNormal(center), ReceiverNormal(other)));
			float w = pow(agreement, lerp(96.0f, 16.0f, roughness)) * saturate(roughness * 4.0f);
			w *= exp2(-0.42f * float(x * x + y * y));
			sum += color * w;
			total += w;
			lower = min(lower, color);
			upper = max(upper, color);
		}
	}
	float3 filtered = sum / total;
	float length = 1.0f;
	if (radianceHistoryLimit > 1u && (center.material & (1u << 31u)) != 0u) {
		uint corner = center.sampleCount >> 30u;
		int2 previous = int2(floor(float2(pixel) - gMotion.Load(int3(pixel, 0)) * viewportSize)) + int2(corner & 1u, corner >> 1u);
		float4 history = UnpackReSTIRRadiance(gReSTIRPrevious[previous.y * viewportSize.x + previous.x]);
		length = history.a < 64.0f ? min(history.a + 1.0f, float(radianceHistoryLimit)) : 1.0f;
		float3 margin = (upper - lower) * 0.25f;
		float3 previousColor = radianceHistoryLimit >= 32u ? history.rgb : clamp(history.rgb, lower - margin, upper + margin);
		filtered = lerp(previousColor, filtered, rcp(length));
	}
	gReSTIRRadianceOut[address] = PackReSTIRRadiance(filtered, length);
}
