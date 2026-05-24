#include "RenderPassExecutionHelper.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RHI/DirectX12/Core/D3D12CommandContext.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

namespace {

	void DispatchInternal(Engine::GraphicsCore& graphicsCore, Engine::SceneExecutionContext& context,
		const std::vector<const Engine::RenderItem*>& items, const Engine::RenderPipelineDeps& deps,
		Engine::MultiRenderTarget* target, const char* drawPassName,
		bool forceVertexMeshVariant, bool depthOnly) {

		if (!target || !deps.dispatcher || !deps.backendRegistry ||
			!deps.assetLibrary || !deps.pipelineCache || !deps.materialResolver) {
			return;
		}

		if (items.empty()) {
			return;
		}

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

		if (depthOnly) {

			if (auto* depth = target->GetDepthTexture()) {
				depth->Transition(*dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);
				dxCommand->BindRenderTargets(std::nullopt, depth->GetDSVCPUHandle());
			} else {
				return;
			}
		} else {

			target->TransitionForRender(*dxCommand);
			target->Bind(*dxCommand);
		}

		if (context.useViewportRect) {
			dxCommand->SetViewportAndScissor(
				context.viewportX, context.viewportY,
				context.viewportWidth, context.viewportHeight);
		} else {
			dxCommand->SetViewportAndScissor(target->GetWidth(), target->GetHeight());
		}

		const bool prevForce = context.forceVertexMeshVariant;
		context.forceVertexMeshVariant = forceVertexMeshVariant || prevForce;

		deps.dispatcher->Dispatch(graphicsCore, context, *deps.renderBatch,
			*deps.backendRegistry, *deps.assetLibrary, *deps.pipelineCache,
			*deps.materialResolver, items, target, drawPassName, false);

		context.forceVertexMeshVariant = prevForce;
	}
}

namespace Engine::RenderPassExecutionHelper {

	void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
		const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
		const char* phaseName, MultiRenderTarget* target, const char* drawPassName,
		bool forceVertexMeshVariant) {

		const RenderPassItemList* list = passBuckets.Find(phaseName);
		if (!list) {
			return;
		}
		DispatchInternal(graphicsCore, context, list->items, deps, target, drawPassName, forceVertexMeshVariant, false);
	}

	void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
		const std::vector<const RenderItem*>& items, const RenderPipelineDeps& deps,
		MultiRenderTarget* target, const char* drawPassName,
		bool forceVertexMeshVariant, bool depthOnly) {

		DispatchInternal(graphicsCore, context, items, deps, target, drawPassName, forceVertexMeshVariant, depthOnly);
	}

} // Engine::RenderPassExecutionHelper
