#include "RenderPassExecutionHelper.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxCommandContext.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

namespace {

	void DispatchInternal(Engine::GraphicsCore& graphicsCore, Engine::SceneExecutionContext& context,
		const std::vector<const Engine::RenderItem*>& items, const Engine::RenderPipelineDeps& deps,
		const Engine::RenderPassSurfaceBinding& surface, const char* drawPassName,
		bool forceVertexMeshVariant, bool depthOnly) {

		Engine::MultiRenderTarget* target = surface.colorSurface;
		if (!target || !deps.dispatcher || !deps.backendRegistry ||
			!deps.assetLibrary || !deps.pipelineCache || !deps.materialResolver) {
			return;
		}

		if (items.empty()) {
			return;
		}

		// 外部DSV指定があればそちらを使う。なければサーフェス自身の深度を使う
		Engine::DepthTexture2D* depth = surface.depthOverride
			? surface.depthOverride
			: target->GetDepthTexture();

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

		if (depthOnly) {

			if (depth) {
				depth->Transition(*dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);
				dxCommand->BindRenderTargets(std::nullopt, depth->GetDSVCPUHandle());
			} else {
				return;
			}
		} else if (surface.depthOverride) {

			// 色サーフェスのRTVと外部DSVを明示的に組み合わせてバインドする
			std::vector<Engine::RenderTarget> renderTargets{};
			renderTargets.reserve(target->GetColorCount());
			for (uint32_t i = 0; i < target->GetColorCount(); ++i) {

				auto* color = target->GetColorTexture(i);
				if (!color) {
					continue;
				}
				color->Transition(*dxCommand, D3D12_RESOURCE_STATE_RENDER_TARGET);
				renderTargets.emplace_back(color->GetRenderTarget());
			}
			if (depth) {
				depth->Transition(*dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			}
			dxCommand->BindRenderTargets(renderTargets,
				depth ? std::optional<D3D12_CPU_DESCRIPTOR_HANDLE>(depth->GetDSVCPUHandle()) : std::nullopt);
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

		// depthOnly/外部DSVのフォーマット解決が正しく行われるよう、depthOverrideとdepthOnlyを渡す
		deps.dispatcher->Dispatch(graphicsCore, context, *deps.renderBatch,
			*deps.backendRegistry, *deps.assetLibrary, *deps.pipelineCache,
			*deps.materialResolver, items, target, surface.depthOverride, drawPassName, depthOnly);

		context.forceVertexMeshVariant = prevForce;
	}
}

namespace Engine::RenderPassExecutionHelper {

	void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
		const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
		RenderPhase phase, MultiRenderTarget* target, const char* drawPassName,
		bool forceVertexMeshVariant) {

		const RenderPassItemList* list = passBuckets.Find(phase);
		if (!list) {
			return;
		}
		DispatchInternal(graphicsCore, context, list->items, deps,
			RenderPassSurfaceBinding{ target, nullptr }, drawPassName, forceVertexMeshVariant, false);
	}

	void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
		const std::vector<const RenderItem*>& items, const RenderPipelineDeps& deps,
		MultiRenderTarget* target, const char* drawPassName,
		bool forceVertexMeshVariant, bool depthOnly) {

		DispatchInternal(graphicsCore, context, items, deps,
			RenderPassSurfaceBinding{ target, nullptr }, drawPassName, forceVertexMeshVariant, depthOnly);
	}

	void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
		const std::vector<const RenderItem*>& items, const RenderPipelineDeps& deps,
		const RenderPassSurfaceBinding& surface, const char* drawPassName,
		bool forceVertexMeshVariant, bool depthOnly) {

		DispatchInternal(graphicsCore, context, items, deps, surface, drawPassName, forceVertexMeshVariant, depthOnly);
	}

} // Engine::RenderPassExecutionHelper
