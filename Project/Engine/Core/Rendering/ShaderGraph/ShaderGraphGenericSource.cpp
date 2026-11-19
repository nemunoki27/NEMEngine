#include "ShaderGraphGenericSource.h"

//============================================================================
//	include
//============================================================================

namespace Engine::ShaderGraphStageSource {

	std::string BuildGenericTypes() {

		return R"hlsl(
struct ShaderGraphGradient {
	uint colorCount;
	uint alphaCount;
	uint fixedMode;
	float4 colors[8];
	float2 alphas[8];
};
)hlsl";
	}

	std::string BuildGenericFunctions(const ShaderGraphAsset& graph, bool bindlessTextures) {

		std::string source = R"hlsl(
float4x4 ShaderGraphInverse4x4(float4x4 value) {
	float4x4 result = float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
	[unroll] for (uint column = 0; column < 4; ++column) {
		uint pivot = column;
		[unroll] for (uint row = column + 1; row < 4; ++row) {
			if (abs(value[pivot][column]) < abs(value[row][column])) { pivot = row; }
		}
		if (abs(value[pivot][column]) < 1e-12) { return (float4x4)0; }
		float4 rowValue = value[column];
		value[column] = value[pivot];
		value[pivot] = rowValue;
		rowValue = result[column];
		result[column] = result[pivot];
		result[pivot] = rowValue;
		float reciprocal = rcp(value[column][column]);
		value[column] *= reciprocal;
		result[column] *= reciprocal;
		[unroll] for (uint row = 0; row < 4; ++row) {
			if (row == column) { continue; }
			float factor = value[row][column];
			value[row] -= factor * value[column];
			result[row] -= factor * result[column];
		}
	}
	return result;
}

float3 ShaderGraphTransform(float3 value, float4x4 transform, uint type, bool unitLength) {
	if (type == 2u) {
		float3 normal = mul(value, (float3x3)transpose(ShaderGraphInverse4x4(transform)));
		return unitLength ? normalize(normal) : normal;
	}
	float4 result = mul(float4(value, type == 0u ? 1.0 : 0.0), transform);
	if (type == 0u && 1e-7 < abs(result.w)) { result.xyz /= result.w; }
	return type == 1u && unitLength ? normalize(result.xyz) : result.xyz;
}

float3 ShaderGraphRotateAboutAxis(float3 value, float3 axis, float angle) {
	axis = normalize(axis);
	float sine, cosine;
	sincos(angle, sine, cosine);
	return value * cosine + cross(axis, value) * sine + axis * dot(axis, value) * (1.0 - cosine);
}

float3 ShaderGraphBlackbody(float temperature) {
	float t = max(temperature, 1e-4);
	float red = 56100000.0 * pow(t, -1.5) + 148.0;
	float green = t < 6500.0 ? 100.04 * log(t) - 623.6 : 35200000.0 * pow(t, -1.5) + 184.0;
	float blue = 194.18 * log(t) - 1448.6;
	return saturate(float3(red, green, blue) / 255.0) * saturate(t / 1000.0);
}
)hlsl";
		source += BuildGenericColorFunctions();
		source += BuildGenericProceduralFunctions();
		source += BuildGenericTextureFunctions(graph, bindlessTextures);
		return source;
	}
}
