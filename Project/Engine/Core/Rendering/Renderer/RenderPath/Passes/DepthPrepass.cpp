#include "DepthPrepass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>

//============================================================================
//	DepthPrepass classMethods
//============================================================================

void Engine::DepthPrepass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources) {
		return;
	}

	std::vector<const RenderItem*> items = CollectItems(context, passBuckets);
	RenderPassExecutionHelper::Execute(graphicsCore, context, items, deps_,
		context.resources->GetSceneMain(), "ZPrepass", true, true);
}

std::vector<const Engine::RenderItem*> Engine::DepthPrepass::CollectItems(
	const SceneExecutionContext& context, const RenderPassPhaseBuckets& passBuckets) const {

	std::vector<const RenderItem*> result{};
	const RenderPassItemList* list = passBuckets.Find("Opaque");
	if (!list || list->IsEmpty()) {
		return result;
	}
	const ResolvedCameraView* camera = context.view ? context.view->FindCamera(RenderCameraDomain::Perspective) : nullptr;
	if (!camera) {
		return result;
	}

	result.reserve(list->items.size());
	for (const RenderItem* item : list->items) {

		if (!item) {
			continue;
		}
		if (item->backendID != RenderBackendID::Mesh) {
			continue;
		}
		if ((item->visibilityLayerMask & camera->cullingMask) == 0) {
			continue;
		}
		const MeshRenderPayload* payload = deps_.renderBatch->GetPayload<MeshRenderPayload>(*item);
		if (!payload || !payload->enableZPrepass) {
			continue;
		}
		result.emplace_back(item);
	}
	return result;
}
