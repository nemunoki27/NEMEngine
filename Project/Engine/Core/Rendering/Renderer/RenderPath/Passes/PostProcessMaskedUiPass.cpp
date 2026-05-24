#include "PostProcessMaskedUiPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>

//============================================================================
//	PostProcessMaskedUiPass classMethods
//============================================================================

void Engine::PostProcessMaskedUiPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources || !deps_.dispatcher || !deps_.backendRegistry ||
		!deps_.assetLibrary || !deps_.pipelineCache || !deps_.materialResolver) {
		return;
	}

	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
	if (!sceneFinal) {
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
	if (items.empty()) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	sceneFinal->TransitionForRender(*dxCommand);
	sceneFinal->Bind(*dxCommand);
	dxCommand->SetViewportAndScissor(sceneFinal->GetWidth(), sceneFinal->GetHeight());

	deps_.dispatcher->Dispatch(graphicsCore, context, *deps_.renderBatch,
		*deps_.backendRegistry, *deps_.assetLibrary, *deps_.pipelineCache,
		*deps_.materialResolver, items, sceneFinal, "Draw", false);
}
