#include "CameraSurfacePipeline.h"

//============================================================================
//	CameraAmbientOcclusionPipeline functions
//============================================================================
Engine::RenderFeatureProfileAsset Engine::BuildCameraAmbientOcclusionProfile(const CameraCommon& settings, bool opaqueGeometryAllowed) {

	RenderFeatureProfileAsset profile{};
	const float scale = settings.ambientOcclusionQuality == 2 ? 1.0f : 0.5f;
	RenderFeaturePassSettings trace{};
	trace.id = CameraSurfacePassIDs::AmbientOcclusionTrace;
	trace.name = "AO Trace";
	trace.anchor = RenderFeatureAnchor::BeforeLighting;
	trace.material = AssetID{0x4e454d4153534554ull, 0x414f000000000004ull};
	trace.sourceKind = RenderFeatureSourceKind::SceneColor;
	trace.sceneInputs["gSourcePosition"] = "ScenePositionMain";
	trace.sceneInputs["gSourceNormal"] = "SceneNormalMain";
	trace.sceneInputs["gSourceFlags"] = "SceneFlagsMain";
	trace.outputs.push_back({.name = "Raw", .shaderResource = "gDestColor",
		.format = RenderFeatureTextureFormat::RGBA16_FLOAT, .widthScale = scale, .heightScale = scale});
	trace.parameterOverrides.Set("opaqueGeometryAllowed", MaterialParameterValue{.value = uint32_t(opaqueGeometryAllowed)});
	trace.parameterOverrides.Set("cullingMask", MaterialParameterValue{.value = static_cast<uint32_t>(settings.cullingMask) & 0x00ffffffu});
	trace.parameterOverrides.Set("radius", MaterialParameterValue{.value = settings.ambientOcclusionRadius});
	trace.parameterOverrides.Set("sampleCount", MaterialParameterValue{.value =
		settings.ambientOcclusionQuality == 0 ? 2u : settings.ambientOcclusionQuality == 2 ? 8u : 4u});
	profile.passes.emplace_back(std::move(trace));
	RenderFeaturePassSettings filter{};
	filter.id = CameraSurfacePassIDs::AmbientOcclusionFilter;
	filter.name = "AO Filter";
	filter.anchor = RenderFeatureAnchor::BeforeLighting;
	filter.material = AssetID{0x4e454d4153534554ull, 0x414f000000000014ull};
	filter.sourceKind = RenderFeatureSourceKind::PreviousPass;
	filter.sceneInputs["gSourcePosition"] = "ScenePositionMain";
	filter.sceneInputs["gSourceNormal"] = "SceneNormalMain";
	filter.sceneInputs["gSourceFlags"] = "SceneFlagsMain";
	filter.sceneInputs["gSourceMotion"] = "SceneMotionMain";
	filter.outputs.push_back({.name = "Color", .shaderResource = "gDestColor",
		.format = RenderFeatureTextureFormat::RGBA16_FLOAT, .widthScale = scale, .heightScale = scale,
		.history = true, .historyShaderResource = "gHistoryColor"});
	filter.outputs.push_back({.name = "Geometry", .shaderResource = "gHistoryGeometryOutput",
		.format = RenderFeatureTextureFormat::RGBA32_FLOAT, .widthScale = scale, .heightScale = scale,
		.history = true, .historyShaderResource = "gHistoryGeometry"});
	filter.outputs.push_back({.name = "Normal", .shaderResource = "gHistoryNormalOutput",
		.format = RenderFeatureTextureFormat::RG16_FLOAT, .widthScale = scale, .heightScale = scale,
		.history = true, .historyShaderResource = "gHistoryNormal"});
	filter.parameterOverrides.Set("radius", MaterialParameterValue{.value = settings.ambientOcclusionRadius});
	profile.passes.emplace_back(std::move(filter));
	return profile;
}
