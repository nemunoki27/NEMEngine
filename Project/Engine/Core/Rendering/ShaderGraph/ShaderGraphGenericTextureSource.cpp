#include "ShaderGraphGenericSource.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphSourceUtility.h"

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

std::string Engine::ShaderGraphStageSource::BuildGenericTextureFunctions(const ShaderGraphAsset& graph, bool bindlessTextures) {

	const bool particle = !bindlessTextures &&
		(graph.target == ShaderGraphTarget::Particle || graph.target == ShaderGraphTarget::Trail);
	std::string source = "float4 SampleGraphTextureLOD(uint index, float2 uv, SamplerState sampler, float lod, float4 fallbackValue) {\n";
	if (particle) {
		uint32_t index = 0;
		for (const auto& parameter : graph.parameters) {
			if (parameter.type != ShaderGraphValueType::Texture2D) { continue; }
			const auto name = MakeIdentifier(parameter.referenceName.empty() ? parameter.name : parameter.referenceName, parameter.id);
			source += "\tif (index == " + std::to_string(index++) + "u) return " + name + ".SampleLevel(sampler, uv, lod);\n";
		}
		source += "\treturn fallbackValue;\n}\n";
	} else {
		source += "\tif (index == 0xFFFFFFFFu) { return fallbackValue; }\n"
			"\tTexture2D<float4> texture = NEM_TEXTURE2D(index);\n"
			"\treturn texture.SampleLevel(sampler, uv, lod);\n}\n";
	}
	source += "float4 ShaderGraphTextureSize(uint index) {\n\tuint width = 0, height = 0;\n";
	if (particle) {
		uint32_t index = 0;
		for (const auto& parameter : graph.parameters) {
			if (parameter.type != ShaderGraphValueType::Texture2D) { continue; }
			const auto name = MakeIdentifier(parameter.referenceName.empty() ? parameter.name : parameter.referenceName, parameter.id);
			source += "\tif (index == " + std::to_string(index++) + "u) { " + name + ".GetDimensions(width, height); }\n";
		}
	} else {
		source += "\tif (index != 0xFFFFFFFFu) {\n\t\tTexture2D<float4> texture = NEM_TEXTURE2D(index);\n"
			"\t\ttexture.GetDimensions(width, height);\n\t}\n";
	}
	source += "\treturn float4(width, height, width == 0 ? 0.0 : rcp(float(width)), height == 0 ? 0.0 : rcp(float(height)));\n}\n";
	source += R"hlsl(
float2 ShaderGraphFlipbook(float2 uv, float width, float height, float tile, float2 invert) {
	float2 count = max(floor(float2(width, height)), 1.0);
	tile = floor(tile + 1e-5);
	tile -= floor(tile / (count.x * count.y)) * count.x * count.y;
	float row = floor(tile / count.x);
	float column = tile - row * count.x;
	if (0.5 < invert.x) { column = count.x - 1.0 - column; }
	if (0.5 < invert.y) { row = count.y - 1.0 - row; }
	return (uv + float2(column, row)) / count;
}

float2 ShaderGraphRadialShear(float2 uv, float2 center, float2 strength, float2 offset) {
	float2 delta = uv - center;
	return uv + float2(delta.y, -delta.x) * dot(delta, delta) * strength + offset;
}

float2 ShaderGraphSpherize(float2 uv, float2 center, float strength, float2 offset) {
	float2 delta = uv - center;
	float distance = dot(delta, delta);
	return uv + delta * distance * distance * strength + offset;
}

float2 ShaderGraphTwirl(float2 uv, float2 center, float strength, float2 offset) {
	float2 delta = uv - center;
	float angle = strength * length(delta);
	float sine, cosine;
	sincos(angle, sine, cosine);
	return center + float2(cosine * delta.x - sine * delta.y, sine * delta.x + cosine * delta.y) + offset;
}

float4 SampleGraphProjected(uint textureIndex, float2 uv, SamplerState sampler, float4 fallback, float4 settings, bool normalMap) {
#ifdef NEM_SHADER_GRAPH_HEX_TILING
	return SampleGraphHexTexture(textureIndex, BuildHexTiling(uv, settings), sampler, fallback, normalMap);
#else
	return SampleGraphTexture(textureIndex, uv, sampler, fallback);
#endif
}

float SampleGraphHeightLOD(uint textureIndex, float2 uv, SamplerState sampler, float lod, float4 settings) {
#ifdef NEM_SHADER_GRAPH_HEX_TILING
	return SampleGraphHexTextureLOD(textureIndex, BuildHexTiling(uv, settings), sampler, lod, 0.5.xxxx, false).r;
#else
	return SampleGraphTextureLOD(textureIndex, uv, sampler, lod, 0.5.xxxx).r;
#endif
}

float4 ShaderGraphTriplanar(uint textureIndex, SamplerState sampler, float3 position, float3 normal,
	float tile, float blend, bool normalMap, float3x3 tangentToWorld, float4 settings) {
	float3 uv = position * tile;
	float3 weights = pow(abs(normal), max(blend, 0.0));
	weights /= max(dot(weights, 1.0.xxx), 1e-6);
	float4 fallback = normalMap ? float4(0.5, 0.5, 1, 1) : float4(1, 1, 1, 1);
	float4 x = SampleGraphProjected(textureIndex, uv.zy, sampler, fallback, settings, normalMap);
	float4 y = SampleGraphProjected(textureIndex, uv.xz, sampler, fallback, settings, normalMap);
	float4 z = SampleGraphProjected(textureIndex, uv.xy, sampler, fallback, settings, normalMap);
	if (!normalMap) { return x * weights.x + y * weights.y + z * weights.z; }
	float3 nx = x.xyz * 2.0 - 1.0;
	float3 ny = y.xyz * 2.0 - 1.0;
	float3 nz = z.xyz * 2.0 - 1.0;
	nx = float3(nx.xy + normal.zy, abs(nx.z) * normal.x);
	ny = float3(ny.xy + normal.xz, abs(ny.z) * normal.y);
	nz = float3(nz.xy + normal.xy, abs(nz.z) * normal.z);
	float3 worldNormal = normalize(nx.zyx * weights.x + ny.xzy * weights.y + nz.xyz * weights.z);
	return float4(mul(worldNormal, transpose(tangentToWorld)), 1);
}

float2 ShaderGraphParallaxMapping(uint textureIndex, SamplerState sampler, float amplitude, float2 uv, float3 view, float4 settings) {
	float height = SampleGraphProjected(textureIndex, uv, sampler, float4(0.5, 0.5, 0.5, 1), settings, false).r;
	view = normalize(view);
	return uv + (height - 0.5) * amplitude * 0.01 * view.xy / max(view.z + 0.42, 1e-4);
}

float3 ShaderGraphParallaxOcclusion(uint textureIndex, SamplerState sampler, float amplitude, float steps,
	float2 uv, float2 tiling, float2 offset, float2 primitiveSize, float lod, float threshold, float3 view, float4 settings) {
	float maxHeight = amplitude * 0.01 * 2.0 / max(abs(tiling.x) + abs(tiling.y), 1e-5);
	view = normalize(view);
	float2 direction = view.xy * maxHeight * tiling / max(abs(primitiveSize), 1e-5) / max(view.z, 1e-4);
	uint count = (uint)clamp(floor(steps), 1.0, 128.0);
	float layer = rcp(float(count));
	float2 currentUV = uv * tiling + offset;
	float2 startUV = currentUV;
	float currentDepth = 0.0;
	float sampledDepth = 1.0 - SampleGraphHeightLOD(textureIndex, currentUV, sampler, max(lod, 0), settings);
	float previousDelta = -sampledDepth;
	[loop] for (uint i = 0; i < count && currentDepth < sampledDepth; ++i) {
		previousDelta = currentDepth - sampledDepth;
		currentUV -= direction * layer;
		currentDepth += layer;
		sampledDepth = 1.0 - SampleGraphHeightLOD(textureIndex, currentUV, sampler, max(lod, 0), settings);
	}
	float currentDelta = currentDepth - sampledDepth;
	float intersection = saturate(currentDelta / max(currentDelta - previousDelta, 1e-5));
	currentUV += direction * layer * intersection;
	currentDepth -= layer * intersection;
	float fade = 1.0 - saturate(lod - threshold);
	float depthOffset = currentDepth * maxHeight / max(view.z, 1e-4);
	return float3(lerp(startUV, currentUV, fade), depthOffset * fade);
}
)hlsl";
	return source;
}
