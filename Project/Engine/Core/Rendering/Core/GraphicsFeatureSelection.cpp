#include "GraphicsFeatureSelection.h"

Engine::GraphicsRuntimeFeatures Engine::GraphicsFeatureSelection::Resolve(
	const GraphicsFeatureSupport& support, const GraphicsFeaturePreferences& preferences) {

	GraphicsRuntimeFeatures features{};

	// GPU対応が必要な機能はsupportで絞り、カリング系は描画側で安全側に倒せるよう設定を直で反映する
	features.useMeshShader = support.SupportsMeshShaderPath() && preferences.allowMeshShader;
	features.useInlineRayTracing = support.SupportsRayTracingPath() && preferences.allowInlineRayTracing;
	features.useDispatchRays = support.SupportsRayTracingPath() && preferences.allowDispatchRays;
	features.useRaytracingDownsampling =
		preferences.allowRaytracingDownsampling;
	features.softShadowSampleCount =
		preferences.softShadowSampleCount;
	features.useFrustumCulling = preferences.allowFrustumCulling;
	features.useOcclusionCulling = preferences.allowOcclusionCulling;
	features.useContributionCulling = preferences.allowContributionCulling;
	features.useNormalConeCulling = preferences.allowNormalConeCulling;
	features.useMeshLOD = preferences.allowMeshLOD;
	features.meshLOD0PixelThreshold =
		preferences.meshLOD0PixelThreshold;
	features.meshLOD1PixelThreshold =
		preferences.meshLOD1PixelThreshold;
	features.meshLOD2PixelThreshold =
		preferences.meshLOD2PixelThreshold;
	return features;
}
