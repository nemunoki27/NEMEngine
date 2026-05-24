#include "OpaqueRenderPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>

//============================================================================
//	OpaqueRenderPass classMethods
//============================================================================

void Engine::OpaqueRenderPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources || !deps_.dispatcher || !deps_.backendRegistry ||
		!deps_.assetLibrary || !deps_.pipelineCache || !deps_.materialResolver) {
		return;
	}

	MultiRenderTarget* sceneMain = context.resources->GetSceneMain();
	if (!sceneMain) {
		return;
	}

	const RenderPassItemList* list = passBuckets.Find("Opaque");
	if (!list || list->IsEmpty()) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	sceneMain->TransitionForRender(*dxCommand);
	sceneMain->Bind(*dxCommand);
	dxCommand->SetViewportAndScissor(sceneMain->GetWidth(), sceneMain->GetHeight());

	deps_.dispatcher->Dispatch(graphicsCore, context, *deps_.renderBatch,
		*deps_.backendRegistry, *deps_.assetLibrary, *deps_.pipelineCache,
		*deps_.materialResolver, list->items, sceneMain, "Draw", false);
}
