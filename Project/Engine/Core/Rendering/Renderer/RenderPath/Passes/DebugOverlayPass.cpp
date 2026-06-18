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

void Engine::DebugOverlayPass::Execute([[maybe_unused]] GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, [[maybe_unused]] SceneExecutionContext& context) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// SceneViewのときだけグリッドやデバッグ線を最後に重ねる
	if (context.kind == RenderViewKind::Scene && context.defaultSurface && context.view) {
		LineRenderer::GetInstance()->RenderSceneView(
			graphicsCore, *context.view, *context.defaultSurface, context.drawSceneViewDefaultGrid);
	}
#endif
}