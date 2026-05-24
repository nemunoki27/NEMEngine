#include "DebugOverlayPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif

//============================================================================
//	DebugOverlayPass classMethods
//============================================================================

void Engine::DebugOverlayPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	(void)passBuckets;
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	if (context.kind == RenderViewKind::Scene && context.defaultSurface && context.view) {
		LineRenderer::GetInstance()->RenderSceneView(graphicsCore, *context.view, *context.defaultSurface);
	}
#else
	(void)graphicsCore;
	(void)context;
#endif
}
