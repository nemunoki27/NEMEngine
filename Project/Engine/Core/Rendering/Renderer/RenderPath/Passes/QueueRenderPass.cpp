#include "QueueRenderPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTargetCopyUtility.h>

//============================================================================
//	QueueRenderPass classMethods
//============================================================================
void Engine::QueueRenderPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	MultiRenderTarget* target = nullptr;
	switch (desc_.target) {
	case Target::SceneMain:
		if (context.resources) {
			target = context.resources->GetSceneMain();
		}
		break;
	case Target::SceneFinal:
		if (context.resources) {
			target = context.resources->GetSceneFinal();
		}
		break;
	case Target::DefaultSurface:
		target = context.defaultSurface;
		break;
	}
	if (!target) {
		return;
	}
	if (desc_.kind == RenderPathPassKind::Transparent &&
		context.resources) {

		MultiRenderTargetCopy::CopyColor0Resource(
			graphicsCore,
			context.resources->GetSceneFinal(),
			context.resources->GetSceneColorOpaque());
		context.resources->GetSceneColorOpaque()->
			TransitionForShaderRead(
				*graphicsCore.GetDXObject().GetDxCommand());
	}

	if (!desc_.usePhaseExecution) {
		const RenderPassItemList& items = passBuckets.Get(desc_.phase);
		RenderPassExecutionHelper::Execute(graphicsCore, context, items.items, deps_, target);
		return;
	}

	DepthTexture2D* depth = nullptr;
	if (desc_.reuseSceneDepth && context.resources) {
		depth = context.resources->GetSceneMain()->GetDepthTexture();
	}
	RenderPassExecutionHelper::Execute(graphicsCore, context, passBuckets, deps_,
		desc_.phase, target, desc_.materialPass, desc_.forceVertexMeshVariant, depth);
}
