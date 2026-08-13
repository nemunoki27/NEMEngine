#include "BlitToViewPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTargetCopyUtility.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessDebugInjector.h>
#include <Engine/Core/Rendering/PostProcess/Color/ColorPipelineProcessor.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackService.h>

//============================================================================
//	BlitToViewPass classMethods
//============================================================================

void Engine::BlitToViewPass::Execute(GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	// 最終合成結果を出力先へ写すだけなのでバケットは使わない
	if (!context.resources || !context.defaultSurface ||
		!deps_.assetLibrary || !deps_.pipelineCache ||
		!deps_.colorPipelineProcessor) {
		return;
	}

	// SceneFinalをsourceにしdefaultSurfaceへblitする
	MultiRenderTarget* source = context.resources->GetSceneFinal();
	MultiRenderTarget* dest = context.defaultSurface;
	if (!source || !dest) {
		return;
	}

	if (deps_.postProcessDebugInjector && deps_.postProcessExecutor &&
		deps_.postProcessTargetPool && deps_.postProcessAssetGenerator) {

		deps_.postProcessDebugInjector->TryExecuteBeforeBlit(graphicsCore,
			context, Engine::RenderTargetNames::kSceneColorFinal, "View",
			*deps_.assetLibrary, *deps_.pipelineCache,
			*deps_.postProcessExecutor, *deps_.postProcessTargetPool,
			*deps_.postProcessAssetGenerator, source);

		// デバッグPPでdestへのRTVバインドが外れるため再適用する
		dest = context.defaultSurface;
	}

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	service.EnsureLoaded();
	// 露出更新は最終ビュー出力で1フレームに1回だけ行う
	if (!deps_.colorPipelineProcessor->ToneMap(graphicsCore, context,
		source, dest, *deps_.assetLibrary, *deps_.pipelineCache,
		service.GetSettings().colorPipeline, true)) {

		MultiRenderTargetCopy::CopyColor0Resource(graphicsCore, source, dest);
	}
}
