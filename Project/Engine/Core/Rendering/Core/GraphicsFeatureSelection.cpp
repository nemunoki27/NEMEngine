#include "GraphicsFeatureSelection.h"

bool Engine::GraphicsFeatureSelection::SupportsMaterialReflections(const GraphicsFeatureSupport& support) {

	return support.SupportsShaderModel6_6() && support.SupportsInlineRayTracingPath();
}

Engine::GraphicsRuntimeFeatures Engine::GraphicsFeatureSelection::Resolve(
	const GraphicsFeatureSupport& support, const GraphicsFeaturePreferences& preferences) {

	GraphicsRuntimeFeatures features{};

	// GPU対応が必要な機能はsupportで絞り、カリング系は描画側で安全側に倒せるよう設定を直で反映する
	features.useMeshShader = support.SupportsMeshShaderPath() && preferences.allowMeshShader;
	features.useInlineRayTracing = support.SupportsInlineRayTracingPath() && preferences.allowInlineRayTracing;
	features.useAmbientOcclusion = support.SupportsInlineRayTracingPath() && support.SupportsShaderModel6_6() && preferences.ambientOcclusionEnabled;
	features.useDynamicGI = support.SupportsDynamicGI() && preferences.dynamicGI;
	features.useReSTIRDI = support.SupportsReSTIRDI() && preferences.allowInlineRayTracing && preferences.enableReSTIRDI;
	features.useDispatchRays = SupportsMaterialReflections(support) && preferences.reflectionEnabled;
	features.useRaytracingDownsampling =
		preferences.allowRaytracingDownsampling;
	features.softShadowSampleCount =
		preferences.softShadowSampleCount;
	features.useFrustumCulling = preferences.allowFrustumCulling;
	features.useOcclusionCulling = preferences.allowOcclusionCulling;
	features.useContributionCulling = preferences.allowContributionCulling;
	features.useNormalConeCulling = preferences.allowNormalConeCulling;
	return features;
}
