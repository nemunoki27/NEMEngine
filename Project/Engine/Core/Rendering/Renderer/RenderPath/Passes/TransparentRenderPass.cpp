#include "TransparentRenderPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>

//============================================================================
//	TransparentRenderPass classMethods
//============================================================================

void Engine::TransparentRenderPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources || !deps_.dispatcher || !deps_.backendRegistry ||
		!deps_.assetLibrary || !deps_.pipelineCache || !deps_.materialResolver) {
		return;
	}

	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
	if (!sceneFinal) {
		return;
	}

	const RenderPassItemList* list = passBuckets.Find("Transparent");
	if (!list || list->IsEmpty()) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	sceneFinal->TransitionForRender(*dxCommand);
	sceneFinal->Bind(*dxCommand);
	dxCommand->SetViewportAndScissor(sceneFinal->GetWidth(), sceneFinal->GetHeight());

	deps_.dispatcher->Dispatch(graphicsCore, context, *deps_.renderBatch,
		*deps_.backendRegistry, *deps_.assetLibrary, *deps_.pipelineCache,
		*deps_.materialResolver, list->items, sceneFinal, "Draw", false);
}
