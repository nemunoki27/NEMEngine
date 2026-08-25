#include "QueueRenderPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTargetCopyUtility.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>

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

	DepthTexture2D* depth = nullptr;
	if (desc_.reuseSceneDepth && context.resources) {
		depth = context.resources->GetSceneMain()->GetDepthTexture();
	}
	const RenderPassItemList& items = passBuckets.Get(desc_.phase);
	RenderFeatureProfileService& featureService =
		RenderFeatureProfileService::GetInstance();
	featureService.EnsureLoaded();
	itemScratch_.clear();
	itemScratch_.reserve(items.items.size());
	for (const RenderItem* item : items.items) {
		if (item && !featureService.GetRuntime().IsItemIsolated(*item)) {
			itemScratch_.emplace_back(item);
		}
	}
	if (itemScratch_.size() != items.items.size() ||
		!desc_.usePhaseExecution) {

		RenderPassExecutionHelper::Execute(graphicsCore, context,
			itemScratch_, deps_, RenderPassSurfaceBinding{
				.colorSurface = target,
				.depthOverride = depth,
			}, desc_.usePhaseExecution ? desc_.materialPass :
				MaterialPassKind::Draw,
			desc_.forceVertexMeshVariant);
		return;
	}
	RenderPassExecutionHelper::Execute(graphicsCore, context, passBuckets, deps_,
		desc_.phase, target, desc_.materialPass, desc_.forceVertexMeshVariant, depth);
}
