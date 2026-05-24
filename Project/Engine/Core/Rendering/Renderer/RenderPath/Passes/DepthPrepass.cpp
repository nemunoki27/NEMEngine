#include "DepthPrepass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>

//============================================================================
//	DepthPrepass classMethods
//============================================================================

void Engine::DepthPrepass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources || !context.resources->GetSceneMain()) {
		return;
	}
	if (!deps_.dispatcher || !deps_.backendRegistry || !deps_.assetLibrary ||
		!deps_.pipelineCache || !deps_.materialResolver) {
		return;
	}

	MultiRenderTarget* sceneMain = context.resources->GetSceneMain();
	DepthTexture2D* depth = sceneMain->GetDepthTexture();
	if (!depth) {
		return;
	}

	std::vector<const RenderItem*> items = CollectItems(context, passBuckets);
	if (items.empty()) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();

	depth->Transition(*dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	dxCommand->BindRenderTargets(std::nullopt, depth->GetDSVCPUHandle());
	dxCommand->SetViewportAndScissor(sceneMain->GetWidth(), sceneMain->GetHeight());

	deps_.dispatcher->Dispatch(graphicsCore, context, *deps_.renderBatch,
		*deps_.backendRegistry, *deps_.assetLibrary, *deps_.pipelineCache,
		*deps_.materialResolver, items, sceneMain, "ZPrepass", true);
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
