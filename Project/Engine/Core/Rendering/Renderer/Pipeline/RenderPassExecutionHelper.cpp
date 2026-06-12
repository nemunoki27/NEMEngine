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
		const Engine::RenderPassSurfaceBinding& surface, Engine::MaterialPassKind passKind,
		bool forceVertexMeshVariant, bool depthOnly) {

		// 描画先と必須の依存が1つでも欠けていれば何もしない
		Engine::MultiRenderTarget* target = surface.colorSurface;
		if (!target || !deps.dispatcher || !deps.backendRegistry ||
			!deps.assetLibrary || !deps.pipelineCache || !deps.materialResolver) {
			return;
		}

		// 描画アイテムが無ければバインドもせず抜ける
		if (items.empty()) {
			return;
		}

		// 外部DSV指定があればそちらを使い、なければサーフェス自身の深度を使う
		Engine::DepthTexture2D* depth = surface.depthOverride
			? surface.depthOverride
			: target->GetDepthTexture();

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

		// バインドの仕方はdepthOnly/外部DSV併用/通常の3通りに分かれる
		if (depthOnly) {

			// ZPrepassやstencil書き込み用にRTVを付けず深度だけをバインドする
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

			// 通常は色サーフェス自身のRTVと深度をまとめてバインドする
			target->TransitionForRender(*dxCommand);
			target->Bind(*dxCommand);
		}

		// ツールプレビュー等でviewport矩形指定があればそれを使い、無ければtarget全体
		if (context.useViewportRect) {
			dxCommand->SetViewportAndScissor(
				context.viewportX, context.viewportY,
				context.viewportWidth, context.viewportHeight);
		} else {
			dxCommand->SetViewportAndScissor(target->GetWidth(), target->GetHeight());
		}

		// このパスだけ頂点メッシュvariant強制を上書きし、後で元へ戻す
		const bool prevForce = context.forceVertexMeshVariant;
		context.forceVertexMeshVariant = forceVertexMeshVariant || prevForce;

		// depthOnly/外部DSVのフォーマット解決が正しく行われるよう、depthOverrideとdepthOnlyを渡す
		deps.dispatcher->Dispatch(graphicsCore, context, *deps.renderBatch,
			*deps.backendRegistry, *deps.assetLibrary, *deps.pipelineCache,
			*deps.materialResolver, items, target, surface.depthOverride, passKind, depthOnly);

		context.forceVertexMeshVariant = prevForce;
	}
}

namespace Engine::RenderPassExecutionHelper {

	void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
		const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
		RenderPhase phase, MultiRenderTarget* target, MaterialPassKind passKind,
		bool forceVertexMeshVariant) {

		// 指定phaseのバケットをそのままtargetへ流すラッパー
		const RenderPassItemList* list = passBuckets.Find(phase);
		if (!list) {
			return;
		}
		DispatchInternal(graphicsCore, context, list->items, deps,
			RenderPassSurfaceBinding{ target, nullptr }, passKind, forceVertexMeshVariant, false);
	}

	void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
		const std::vector<const RenderItem*>& items, const RenderPipelineDeps& deps,
		MultiRenderTarget* target, MaterialPassKind passKind,
		bool forceVertexMeshVariant, bool depthOnly) {

		// 収集済みアイテム配列を深度のみ込みでtargetへ流すラッパー
		DispatchInternal(graphicsCore, context, items, deps,
			RenderPassSurfaceBinding{ target, nullptr }, passKind, forceVertexMeshVariant, depthOnly);
	}

	void Execute(GraphicsCore& graphicsCore, SceneExecutionContext& context,
		const std::vector<const RenderItem*>& items, const RenderPipelineDeps& deps,
		const RenderPassSurfaceBinding& surface, MaterialPassKind passKind,
		bool forceVertexMeshVariant, bool depthOnly) {

		// 色サーフェスと外部DSVの組み合わせを明示指定するラッパー
		DispatchInternal(graphicsCore, context, items, deps, surface, passKind, forceVertexMeshVariant, depthOnly);
	}

} // Engine::RenderPassExecutionHelper
