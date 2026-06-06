#include "EditorOverlayPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

//============================================================================
//	EditorOverlayPass classMethods
//============================================================================
void Engine::EditorOverlayPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	(void)graphicsCore;
	(void)passBuckets;
	(void)context;
}
