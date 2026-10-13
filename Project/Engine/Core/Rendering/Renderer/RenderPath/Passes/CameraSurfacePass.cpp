#include "CameraSurfacePass.h"

//============================================================================
//	include
//============================================================================
#include "CameraSurfacePipeline.h"
#include "RenderFeatureResourceUtility.h"
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>

// c++
#include <algorithm>

//============================================================================
//	CameraSurfacePass classMethods
//============================================================================
Engine::CameraSurfacePass::CameraSurfacePass(const RenderPipelineDeps& deps, bool ambientOcclusion) :
	ambientOcclusion_(ambientOcclusion), executor_(deps,
		ambientOcclusion ? RenderFeatureAnchor::BeforeLighting : RenderFeatureAnchor::AfterLighting,
		{.sceneColorOutput = !ambientOcclusion, .allowInlineRayTracing = ambientOcclusion, .useConfiguredResolution = true}) {

}

void Engine::CameraSurfacePass::RetainViews(const std::unordered_set<std::string>& activeViews) {

	// 非表示Cameraの設定と履歴をまとめて回収する
	std::erase_if(states_, [&](const auto& state) { return !activeViews.contains(state.first); });
	executor_.RetainViews(activeViews);
}

void Engine::CameraSurfacePass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (ambientOcclusion_) context.ambientOcclusion = nullptr;
	if (!context.view || !context.view->FindCamera(RenderCameraDomain::Perspective) || context.disableInlineRayTracing) return;
	const auto& controller = graphicsCore.GetDXObject().GetFeatureController();
	auto settings = context.postProcessCamera.surfaceSettings;
	settings.cullingMask = static_cast<int32_t>(context.view->GetCullingMask(RenderCameraDomain::Perspective));
	const bool active = ambientOcclusion_ ? settings.ambientOcclusionEnabled &&
		controller.GetSupport().SupportsInlineRayTracingPath() && controller.GetSupport().SupportsShaderModel6_6() :
		settings.reflectionEnabled && controller.GetRuntimeFeatures().useDispatchRays;
	if (!active || !context.raytracing.tlasResource) return;

	// 設定変更時だけ実行計画を再生成する
	nlohmann::json configuration = ambientOcclusion_ ? nlohmann::json{
		settings.ambientOcclusionRadius, settings.ambientOcclusionQuality} : nlohmann::json{
		settings.reflectionQuality, settings.reflectionIntensity, settings.reflectionMaxDistance, settings.reflectionParameters};
	// 投影方式や描画対象が変わった履歴は引き継がない
	const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	for (const auto& row : camera->matrices.projectionMatrix.m) {
		for (float value : row) configuration.push_back(value);
	}
	configuration.push_back(settings.cullingMask);
	State& state = states_[context.view->GetHistoryKey()];
	if (state.settings != configuration) {
		state.runtime.Rebuild(ambientOcclusion_ ? BuildCameraAmbientOcclusionProfile(settings) : BuildCameraReflectionProfile(settings));
		state.settings = configuration;
		++state.generation;
	}

	// 独立したContextでポストプロセスの設定を保持する
	SceneExecutionContext surfaceContext = context;
	surfaceContext.renderPassesRuntime = &state.runtime;
	surfaceContext.renderPassesGeneration = state.generation;
	executor_.Execute(graphicsCore, passBuckets, surfaceContext);
	if (ambientOcclusion_ && executor_.HasExecutionSucceeded()) {
		const auto alias = RenderFeatureResourceUtility::MakeOutputAlias({CameraSurfacePassIDs::AmbientOcclusionFilter, "Color"});
		context.ambientOcclusion = context.targetRegistry->FindColorByName(alias);
	}
}
