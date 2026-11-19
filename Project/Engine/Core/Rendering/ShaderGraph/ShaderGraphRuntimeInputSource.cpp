#include "ShaderGraphRuntimeInputSource.h"

//============================================================================
//	include
//============================================================================

std::string Engine::ShaderGraphStageSource::BuildSurfaceInputDeclaration() {

	return "struct ShaderGraphSurfaceInput {\n"
		"\tfloat2 uv;\n"
		"\tfloat3 worldNormal;\n"
		"\tfloat3 worldPosition;\n"
		"\tfloat3 objectPosition;\n"
		"\tfloat3 objectNormal;\n"
		"\tfloat3 objectTangent;\n"
		"\tfloat3 viewDirection;\n"
		"\tfloat4 screenPosition;\n"
		"\tfloat4 vertexColor;\n"
		"\tfloat3x3 tangentToWorld;\n"
		"\tfloat3 particleVelocity;\n"
		"\tfloat particleAge;\n"
		"\tfloat particleLifetime;\n"
		"\tfloat particleNormalizedAge;\n"
		"\tfloat particleRandom;\n"
		"\tfloat4 particleCustomData1;\n"
		"\tfloat4 particleCustomData2;\n"
		"\tfloat4 particleColor;\n"
		"\tfloat4x4 objectToWorld;\n"
		"\tfloat4x4 worldToObject;\n"
		"\tfloat4x4 worldToView;\n"
		"\tfloat4x4 viewToWorld;\n"
		"\tfloat4x4 worldToClip;\n"
		"\tfloat4x4 clipToWorld;\n"
		"\tfloat4x4 viewToClip;\n"
		"\tfloat4x4 clipToView;\n"
		"\tfloat3 cameraPosition;\n"
		"\tfloat3 cameraDirection;\n"
		"\tfloat4 cameraProjection;\n"
		"\tfloat2 cameraOrthographicSize;\n"
		"\tfloat2 screenSize;\n"
		"\tfloat4 viewport;\n"
		"\tfloat3 worldBoundsMin;\n"
		"\tfloat3 worldBoundsMax;\n"
		"\tuint vertexID;\n"
		"\tbool isFrontFace;\n"
		"};\n\n"
		"float3 ShaderGraphWorldPositionFromPixel(float4 position, ShaderGraphSurfaceInput input) {\n"
		"\tfloat2 ndc = (position.xy - input.viewport.xy) / max(input.viewport.zw, 1.0f.xx) * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);\n"
		"\tfloat4 world = mul(float4(ndc, position.z, 1.0f), input.clipToWorld);\n"
		"\treturn world.xyz / world.w;\n"
		"}\n\n"
		"void SetShaderGraphBoxBounds(inout ShaderGraphSurfaceInput input, float3 minimum, float3 maximum, float4x4 world) {\n"
		"\tfloat3 center = mul(float4((minimum + maximum) * 0.5f, 1.0f), world).xyz;\n"
		"\tfloat3 extent = (maximum - minimum) * 0.5f;\n"
		"\tfloat3 worldExtent = mul(extent, abs((float3x3)world));\n"
		"\tinput.worldBoundsMin = center - worldExtent;\n"
		"\tinput.worldBoundsMax = center + worldExtent;\n"
		"}\n\n"
		"void SetShaderGraphSphereBounds(inout ShaderGraphSurfaceInput input, float3 center, float radius, float4x4 world) {\n"
		"\tfloat3 worldCenter = mul(float4(center, 1.0f), world).xyz;\n"
		"\tfloat3 extent = radius * sqrt(world[0].xyz * world[0].xyz + world[1].xyz * world[1].xyz + world[2].xyz * world[2].xyz);\n"
		"\tinput.worldBoundsMin = worldCenter - extent;\n"
		"\tinput.worldBoundsMax = worldCenter + extent;\n"
		"}\n\n";
}

std::string Engine::ShaderGraphStageSource::BuildTimeConstantsDeclaration() {

	return "cbuffer ShaderGraphTimeConstants : register(b4) {\n"
		"\tfloat shaderGraphTime;\n"
		"\tfloat shaderGraphDeltaTime;\n"
		"\tfloat shaderGraphSmoothDeltaTime;\n"
		"\tfloat shaderGraphUnscaledTime;\n"
		"\tfloat4x4 shaderGraphWorldToView;\n"
		"\tfloat4x4 shaderGraphViewToWorld;\n"
		"\tfloat4x4 shaderGraphViewToClip;\n"
		"\tfloat4x4 shaderGraphClipToView;\n"
		"\tfloat4x4 shaderGraphWorldToClip;\n"
		"\tfloat4x4 shaderGraphClipToWorld;\n"
		"\tfloat4 shaderGraphCameraPosition;\n"
		"\tfloat4 shaderGraphCameraDirection;\n"
		"\tfloat4 shaderGraphCameraProjection;\n"
		"\tfloat4 shaderGraphScreenSize;\n"
		"\tfloat4 shaderGraphViewport;\n"
		"};\n\n";
}

std::string Engine::ShaderGraphStageSource::BuildFrameInputAssignments(std::string_view input) {

	std::string source;
	const auto assign = [&](std::string_view member, std::string_view value) {
		source += "\t" + std::string(input) + "." + std::string(member) + " = " + std::string(value) + ";\n";
	};
	assign("worldToView", "shaderGraphWorldToView");
	assign("viewToWorld", "shaderGraphViewToWorld");
	assign("viewToClip", "shaderGraphViewToClip");
	assign("clipToView", "shaderGraphClipToView");
	assign("worldToClip", "shaderGraphWorldToClip");
	assign("clipToWorld", "shaderGraphClipToWorld");
	assign("cameraPosition", "shaderGraphCameraPosition.xyz");
	assign("cameraDirection", "shaderGraphCameraDirection.xyz");
	assign("cameraProjection", "shaderGraphCameraProjection");
	assign("cameraOrthographicSize", "shaderGraphScreenSize.zw");
	assign("screenSize", "shaderGraphScreenSize.xy");
	assign("viewport", "shaderGraphViewport");
	return source;
}

std::string Engine::ShaderGraphStageSource::BuildObjectInputAssignments(std::string_view worldMatrix,
	std::string_view boundsMinimum, std::string_view boundsMaximum, std::string_view input) {

	const std::string prefix = "\t" + std::string(input) + ".";
	return prefix + "objectToWorld = " + std::string(worldMatrix) + ";\n" +
		prefix + "worldToObject = ShaderGraphInverse4x4(" + std::string(input) + ".objectToWorld);\n" +
		prefix + "worldBoundsMin = " + std::string(boundsMinimum) + ";\n" +
		prefix + "worldBoundsMax = " + std::string(boundsMaximum) + ";\n";
}

std::string Engine::ShaderGraphStageSource::BuildSphereBoundsAssignments(std::string_view center,
	std::string_view radius, std::string_view worldMatrix, std::string_view input) {

	return "\tSetShaderGraphSphereBounds(" + std::string(input) + ", " + std::string(center) + ", " +
		std::string(radius) + ", " + std::string(worldMatrix) + ");\n";
}

std::string Engine::ShaderGraphStageSource::BuildBoxBoundsAssignments(std::string_view minimum,
	std::string_view maximum, std::string_view worldMatrix, std::string_view input) {

	return "\tSetShaderGraphBoxBounds(" + std::string(input) + ", " + std::string(minimum) + ", " +
		std::string(maximum) + ", " + std::string(worldMatrix) + ");\n";
}
