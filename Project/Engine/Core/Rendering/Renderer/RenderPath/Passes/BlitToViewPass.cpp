#include "BlitToViewPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessDebugInjector.h>

// c++
#include <vector>

//============================================================================
//	BlitToViewPass classMethods
//============================================================================

namespace {

	constexpr const char* kToneMapToViewMaterialPath =
		"Engine/Assets/Materials/Builtin/ToneMapToView/toneMapToView.material.json";

	bool BindColorTargetsOnly(Engine::GraphicsCore& graphicsCore, Engine::MultiRenderTarget* target) {

		if (!target || target->GetColorCount() == 0) {
			return false;
		}

		std::vector<Engine::RenderTarget> renderTargets{};
		renderTargets.reserve(target->GetColorCount());

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		for (uint32_t i = 0; i < target->GetColorCount(); ++i) {

			Engine::RenderTexture2D* color = target->GetColorTexture(i);
			if (!color) {
				return false;
			}
			color->Transition(*dxCommand, D3D12_RESOURCE_STATE_RENDER_TARGET);
			renderTargets.emplace_back(color->GetRenderTarget());
		}

		dxCommand->BindRenderTargets(renderTargets, std::nullopt);
		dxCommand->SetViewportAndScissor(target->GetWidth(), target->GetHeight());
		return true;
	}

	bool CopyColor0Resource(Engine::GraphicsCore& graphicsCore,
		Engine::MultiRenderTarget* source, Engine::MultiRenderTarget* dest) {

		if (!source || !dest) {
			return false;
		}
		Engine::RenderTexture2D* sourceColor = source->GetColorTexture(0);
		Engine::RenderTexture2D* destColor = dest->GetColorTexture(0);
		if (!sourceColor || !destColor ||
			sourceColor->GetFormat() != destColor->GetFormat() ||
			sourceColor->GetRenderTarget().width != destColor->GetRenderTarget().width ||
			sourceColor->GetRenderTarget().height != destColor->GetRenderTarget().height) {
			return false;
		}

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		sourceColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_SOURCE);
		destColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_DEST);
		dxCommand->GetCommandList()->CopyResource(destColor->GetResource(), sourceColor->GetResource());
		return true;
	}

	bool ExecuteFullscreenBlit(Engine::GraphicsCore& graphicsCore,
		const Engine::SceneExecutionContext& context,
		Engine::MultiRenderTarget* source, Engine::MultiRenderTarget* dest,
		Engine::RenderAssetLibrary& assetLibrary, Engine::PipelineStateCache& pipelineCache) {

		if (!source || !dest || !context.assetDatabase) {
			return false;
		}

		Engine::AssetID resolvedID = context.assetDatabase->ImportOrGet(
			kToneMapToViewMaterialPath, Engine::AssetType::Material);
		const Engine::MaterialAsset* material = assetLibrary.LoadMaterial(resolvedID);
		if (!material) {
			return false;
		}

		const Engine::MaterialPassBinding* passBinding = FindPass(*material, "Blit");
		if (!passBinding) {
			passBinding = FindPass(*material, "Fullscreen");
		}
		if (!passBinding ||
			passBinding->preferredVariant == Engine::PipelineVariantKind::Compute ||
			passBinding->preferredVariant == Engine::PipelineVariantKind::Raytracing) {
			return false;
		}

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

		source->TransitionForShaderRead(*dxCommand);
		if (!BindColorTargetsOnly(graphicsCore, dest)) {
			return false;
		}

		dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

		commandList->SetGraphicsRootSignature(pipelineState->GetRootSignature());
		commandList->SetPipelineState(pipelineState->GetGraphicsPipeline(Engine::BlendMode::Normal));

		const Engine::RootBindingLocation* binding = pipelineState->FindBinding(Engine::ShaderBindingKind::SRV, 0, 0);
		Engine::RenderTexture2D* color = source->GetColorTexture(0);
		if (!binding || !color) {
			return false;
		}
		commandList->SetGraphicsRootDescriptorTable(binding->rootParameterIndex, color->GetSRVGPUHandle());

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);
		return true;
	}
}

void Engine::BlitToViewPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	(void)passBuckets;
	if (!context.resources || !context.defaultSurface ||
		!deps_.assetLibrary || !deps_.pipelineCache) {
		return;
	}

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

	if (!ExecuteFullscreenBlit(graphicsCore, context, source, dest,
		*deps_.assetLibrary, *deps_.pipelineCache)) {

		CopyColor0Resource(graphicsCore, source, dest);
	}
}
