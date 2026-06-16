#include "BlitToViewPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTargetCopyUtility.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessDebugInjector.h>

// c++
#include <vector>

//============================================================================
//	BlitToViewPass classMethods
//============================================================================
namespace {

	// 深度を使わず色RTだけをbindするblit専用のヘルパ
	bool BindColorTargetsOnly(Engine::GraphicsCore& graphicsCore, Engine::MultiRenderTarget* target) {

		if (!target || target->GetColorCount() == 0) {
			return false;
		}

		std::vector<Engine::RenderTarget> renderTargets{};
		renderTargets.reserve(target->GetColorCount());

		// 全色をRTV書き込み状態へ遷移しつつbind対象を集める
		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		for (uint32_t i = 0; i < target->GetColorCount(); ++i) {

			Engine::RenderTexture2D* color = target->GetColorTexture(i);
			if (!color) {
				return false;
			}
			color->Transition(*dxCommand, D3D12_RESOURCE_STATE_RENDER_TARGET);
			renderTargets.emplace_back(color->GetRenderTarget());
		}

		// 深度はnulloptで渡しviewportをdest解像度へ合わせる
		dxCommand->BindRenderTargets(renderTargets, std::nullopt);
		dxCommand->SetViewportAndScissor(target->GetWidth(), target->GetHeight());
		return true;
	}

	bool ExecuteFullscreenBlit(Engine::GraphicsCore& graphicsCore,
		const Engine::SceneExecutionContext& context,
		Engine::MultiRenderTarget* source, Engine::MultiRenderTarget* dest,
		Engine::RenderAssetLibrary& assetLibrary, Engine::PipelineStateCache& pipelineCache,
		Engine::PipelineBindingCache& srvCache, Engine::PipelineBindingCache::SlotID srcColorSlot) {

		if (!source || !dest || !context.assetDatabase) {
			return false;
		}

		// ビルトインMaterialはパスではなく.meta GUIDで固定参照する
		Engine::AssetID resolvedID = Engine::BuiltinAssets::Materials::ToneMapToView;
		const Engine::MaterialAsset* material = assetLibrary.LoadMaterial(resolvedID);
		if (!material) {
			return false;
		}

		// Blitパスを優先しFullscreenへfallback、Compute/Raytracing variantはこの全画面描画では使わない
		const Engine::MaterialPassBinding* passBinding = FindPass(*material, Engine::MaterialPassKind::Blit);
		if (!passBinding) {
			passBinding = FindPass(*material, Engine::MaterialPassKind::Fullscreen);
		}
		if (!passBinding ||
			passBinding->preferredVariant == Engine::PipelineVariantKind::Compute ||
			passBinding->preferredVariant == Engine::PipelineVariantKind::Raytracing) {
			return false;
		}

		// destの色formatを並べてPSOのRTVformatに渡す
		std::array<DXGI_FORMAT, 8> rtvFormats{};
		uint32_t numRTVFormats = 0;
		rtvFormats.fill(DXGI_FORMAT_UNKNOWN);
		for (uint32_t i = 0; i < (std::min)(dest->GetColorCount(), static_cast<uint32_t>(rtvFormats.size())); ++i) {
			if (const auto* color = dest->GetColorTexture(i)) {
				rtvFormats[numRTVFormats++] = color->GetFormat();
			}
		}

		const Engine::PipelineState* pipelineState = pipelineCache.GetORCreate(graphicsCore.GetDXObject(),
			assetLibrary, passBinding->pipeline, passBinding->preferredVariant,
			std::span<const DXGI_FORMAT>(rtvFormats.data(), numRTVFormats), DXGI_FORMAT_UNKNOWN);
		if (!pipelineState) {
			return false;
		}

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		auto* commandList = dxCommand->GetCommandList();

		// sourceをSRV読み取りへ、destを色RTだけのbindへ揃える
		source->TransitionForShaderRead(*dxCommand);
		if (!BindColorTargetsOnly(graphicsCore, dest)) {
			return false;
		}

		dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

		commandList->SetGraphicsRootSignature(pipelineState->GetRootSignature());
		commandList->SetPipelineState(pipelineState->GetGraphicsPipeline(Engine::BlendMode::Normal));

		// pipelineに合わせてslotを解決しsourceの色をt0へbindする
		srvCache.Sync(*pipelineState);
		Engine::RenderTexture2D* color = source->GetColorTexture(0);
		if (!srvCache.Has(srcColorSlot) || !color) {
			return false;
		}
		Engine::RootBindingCommand::SetGraphicsSRV(commandList, srvCache.Get(srcColorSlot), 0, color->GetSRVGPUHandle());

		// 頂点バッファ無しの全画面三角形を1枚描いてtonemap blitする
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);
		return true;
	}
}

void Engine::BlitToViewPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	// 最終合成結果を出力先へ写すだけなのでバケットは使わない
	(void)passBuckets;
	if (!context.resources || !context.defaultSurface ||
		!deps_.assetLibrary || !deps_.pipelineCache) {
		return;
	}

	// SceneFinalをsourceにしdefaultSurfaceへblitする
	MultiRenderTarget* source = context.resources->GetSceneFinal();
	MultiRenderTarget* dest = context.defaultSurface;
	if (!source || !dest) {
		return;
	}

	if (deps_.postProcessDebugInjector && deps_.postProcessExecutor &&
		deps_.postProcessTargetPool && deps_.postProcessAssetGenerator) {

		deps_.postProcessDebugInjector->TryExecuteBeforeBlit(graphicsCore,
			context, "SceneColorFinal", "View",
			*deps_.assetLibrary, *deps_.pipelineCache,
			*deps_.postProcessExecutor, *deps_.postProcessTargetPool,
			*deps_.postProcessAssetGenerator, source);

		// デバッグPPでdestへのRTVバインドが外れるため再適用する
		dest = context.defaultSurface;
	}

	// 通常はtonemap付きの全画面blit、material解決やPSO構築に失敗したときだけresource copyへ退避する
	if (!ExecuteFullscreenBlit(graphicsCore, context, source, dest,
		*deps_.assetLibrary, *deps_.pipelineCache, blitSRVCache_, srcColorSlot_)) {

		MultiRenderTargetCopy::CopyColor0Resource(graphicsCore, source, dest);
	}
}
