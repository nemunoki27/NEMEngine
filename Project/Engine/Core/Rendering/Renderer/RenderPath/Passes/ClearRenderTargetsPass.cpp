#include "ClearRenderTargetsPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

//============================================================================
//	ClearRenderTargetsPass classMethods
//============================================================================
void Engine::ClearRenderTargetsPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	(void)passBuckets;
	if (!context.resources || !context.resources->IsValid()) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();

	MultiRenderTarget* sceneMain = context.resources->GetSceneMain();
	MultiRenderTargetClearDesc clearDesc{};
	clearDesc.clearColor = true;
	clearDesc.clearDepth = true;
	clearDesc.clearDepthValue = 1.0f;
	clearDesc.clearStencil = false;

	sceneMain->TransitionForRender(*dxCommand);
	sceneMain->Bind(*dxCommand);
	dxCommand->SetViewportAndScissor(sceneMain->GetWidth(), sceneMain->GetHeight());
	sceneMain->Clear(*dxCommand, clearDesc);

	if (context.defaultSurface) {

		MultiRenderTargetClearDesc surfaceClearDesc{};
		surfaceClearDesc.clearColor = true;
		surfaceClearDesc.clearDepth = (context.defaultSurface->GetDepthTexture() != nullptr);
		surfaceClearDesc.clearDepthValue = 1.0f;
		surfaceClearDesc.clearStencil = false;

		context.defaultSurface->TransitionForRender(*dxCommand);
		context.defaultSurface->Bind(*dxCommand);
		dxCommand->SetViewportAndScissor(context.defaultSurface->GetWidth(), context.defaultSurface->GetHeight());
		context.defaultSurface->Clear(*dxCommand, surfaceClearDesc);
	}
}
