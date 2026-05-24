#include "PostProcessMaskedUiPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>

//============================================================================
//	PostProcessMaskedUiPass classMethods
//============================================================================

void Engine::PostProcessMaskedUiPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources) {
		return;
	}

	// postProcessTarget==true のアイテムをすべてのフェーズから収集する
	std::vector<const RenderItem*> items{};
	for (const auto& [phase, list] : passBuckets.phaseToItems) {
		for (const RenderItem* item : list.items) {
			if (item && item->postProcessTarget) {
				items.emplace_back(item);
			}
		}
	}

	RenderPassExecutionHelper::Execute(graphicsCore, context, items, deps_,
		context.resources->GetSceneFinal());
}
