#include "ScreenUiPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>

//============================================================================
//	ScreenUiPass classMethods
//============================================================================
void Engine::ScreenUiPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	// ViewportへのBlit後に重ねるScreen UIだけを描画する
	const RenderPassItemList& items = passBuckets.Get(RenderPhase::ScreenUI);

	RenderPassExecutionHelper::Execute(graphicsCore, context, items.items, deps_,
		context.defaultSurface);
}
