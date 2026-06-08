#include "EditorOverlayPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayState.h>

//============================================================================
//	EditorOverlayPass classMethods
//============================================================================
void Engine::EditorOverlayPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	(void)passBuckets;
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	if (context.kind != RenderViewKind::Scene ||
		!context.allowSceneComponentOverlay ||
		!context.world || !context.view || !context.view->valid ||
		!context.defaultSurface || !context.resources || !context.assetDatabase) {
		SceneComponentOverlayState::GetInstance().Clear(context.world);
		return;
	}

	collector_.Collect(*context.world, *context.view, registry_, settings_, items_);
	if (items_.empty()) {
		SceneComponentOverlayState::GetInstance().Clear(context.world);
		return;
	}
	DepthTexture2D* sceneDepth = nullptr;
	if (MultiRenderTarget* sceneMain = context.resources->GetSceneMain()) {
		sceneDepth = sceneMain->GetDepthTexture();
	}
	renderer_.Render(graphicsCore, *context.assetDatabase, *context.view,
		*context.defaultSurface, sceneDepth, context.world, items_);
#else
	(void)graphicsCore;
	(void)context;
#endif
}
