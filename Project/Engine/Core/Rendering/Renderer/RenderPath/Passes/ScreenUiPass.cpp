#include "ScreenUiPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>

//============================================================================
//	ScreenUiPass classMethods
//============================================================================

void Engine::ScreenUiPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.defaultSurface || !deps_.dispatcher || !deps_.backendRegistry ||
		!deps_.assetLibrary || !deps_.pipelineCache || !deps_.materialResolver) {
		return;
	}

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
	if (items.empty()) {
		return;
	}

	MultiRenderTarget* dest = context.defaultSurface;
	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	dest->TransitionForRender(*dxCommand);
	dest->Bind(*dxCommand);
	dxCommand->SetViewportAndScissor(dest->GetWidth(), dest->GetHeight());

	deps_.dispatcher->Dispatch(graphicsCore, context, *deps_.renderBatch,
		*deps_.backendRegistry, *deps_.assetLibrary, *deps_.pipelineCache,
		*deps_.materialResolver, items, dest, "Draw", false);
}
