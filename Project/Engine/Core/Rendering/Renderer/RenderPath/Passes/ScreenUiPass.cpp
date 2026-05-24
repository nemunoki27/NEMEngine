#include "ScreenUiPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>

//============================================================================
//	ScreenUiPass classMethods
//============================================================================

void Engine::ScreenUiPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	// postProcessTarget==false かつ Opaque/Transparent 以外のアイテムを収集する
	std::vector<const RenderItem*> items{};
	for (const auto& [phase, list] : passBuckets.phaseToItems) {
		if (phase == "Opaque" || phase == "Transparent") {
			continue;
		}
		for (const RenderItem* item : list.items) {
			if (item && !item->postProcessTarget) {
				items.emplace_back(item);
			}
		}
	}

	RenderPassExecutionHelper::Execute(graphicsCore, context, items, deps_,
		context.defaultSurface);
}
