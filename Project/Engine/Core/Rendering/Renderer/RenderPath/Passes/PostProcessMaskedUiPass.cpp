#include "PostProcessMaskedUiPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>

//============================================================================
//	PostProcessMaskedUiPass classMethods
//============================================================================

void Engine::PostProcessMaskedUiPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources) {
		return;
	}

	// ポストプロセス適用前にSceneFinalへ合成するUIだけを描画する
	const RenderPassItemList& items = passBuckets.Get(RenderPhase::PostProcessMaskedUI);

	RenderPassExecutionHelper::Execute(graphicsCore, context, items.items, deps_,
		context.resources->GetSceneFinal());
}
