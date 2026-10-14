#include "CameraSurfacePass.h"

//============================================================================
//	include
//============================================================================
#include "CameraSurfacePipeline.h"
#include "RenderFeatureResourceUtility.h"
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Foundation/Utility/ScopedValue.h>

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
	const auto& settings = context.postProcessCamera.surfaceSettings;
	const int32_t cullingMask = static_cast<int32_t>(context.view->GetCullingMask(RenderCameraDomain::Perspective));
	const bool active = ambientOcclusion_ ? settings.ambientOcclusionEnabled &&
		controller.GetSupport().SupportsInlineRayTracingPath() && controller.GetSupport().SupportsShaderModel6_6() :
		settings.reflectionEnabled && controller.GetRuntimeFeatures().useDispatchRays;
	if (!active || !context.raytracing.tlasResource) return;

	// 設定変更時だけ実行計画を再生成する
	// 投影方式や描画対象が変わった履歴は引き継がない
	const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	State& state = states_[context.view->GetHistoryKey()];
	const bool parametersChanged = ambientOcclusion_ ?
		state.settings.ambientOcclusionRadius != settings.ambientOcclusionRadius ||
		state.settings.ambientOcclusionQuality != settings.ambientOcclusionQuality :
		state.settings.reflectionQuality != settings.reflectionQuality ||
		state.settings.reflectionIntensity != settings.reflectionIntensity ||
		state.settings.reflectionMaxDistance != settings.reflectionMaxDistance ||
		state.settings.reflectionParameters != settings.reflectionParameters;
	if (state.generation == 0 || parametersChanged || state.settings.cullingMask != cullingMask ||
		state.projectionMatrix != camera->matrices.projectionMatrix) {

		// 可変設定のコピーは計画を作り直すときだけ行う
		CameraCommon runtimeSettings = settings;
		runtimeSettings.cullingMask = cullingMask;
		state.runtime.Rebuild(ambientOcclusion_ ? BuildCameraAmbientOcclusionProfile(runtimeSettings) : BuildCameraReflectionProfile(runtimeSettings));
		state.settings = std::move(runtimeSettings);
		state.projectionMatrix = camera->matrices.projectionMatrix;
		++state.generation;
	}

	// 実行中だけ固定パスの計画を参照する
	const ScopedValue runtime(context.renderPassesRuntime, &state.runtime);
	const ScopedValue generation(context.renderPassesGeneration, state.generation);
	executor_.Execute(graphicsCore, passBuckets, context);
	if (ambientOcclusion_ && executor_.HasExecutionSucceeded()) {
		const auto alias = RenderFeatureResourceUtility::MakeOutputAlias({CameraSurfacePassIDs::AmbientOcclusionFilter, "Color"});
		context.ambientOcclusion = context.targetRegistry->FindColorByName(alias);
	}
}
