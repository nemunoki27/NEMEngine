#include "PostProcessStackPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessExecutor.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessTemporaryTargetPool.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackService.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

// c++
#include <algorithm>
#include <cstring>
#include <array>
#include <span>
#include <vector>
#include <optional>

//============================================================================
//	PostProcessStackPass classMethods
//============================================================================

namespace {

	constexpr const char* kSceneColorFinal = Engine::RenderTargetNames::kSceneColorFinal;
	constexpr const char* kPingName = "PostProcessPing";
	constexpr const char* kPongName = "PostProcessPong";

	bool CopyColor0Resource(Engine::GraphicsCore& graphicsCore,
		Engine::MultiRenderTarget* source, Engine::MultiRenderTarget* dest) {

		if (!source || !dest) {
			return false;
		}
		Engine::RenderTexture2D* sourceColor = source->GetColorTexture(0);
		Engine::RenderTexture2D* destColor = dest->GetColorTexture(0);
		if (!sourceColor || !destColor) {
			return false;
		}

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		sourceColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_SOURCE);
		destColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_DEST);
		dxCommand->GetCommandList()->CopyResource(destColor->GetResource(), sourceColor->GetResource());
		dest->TransitionForShaderRead(*dxCommand);
		// sourceをCOPY_SOURCEのまま残すと、直後にsourceを入力読みするパスとの間で状態追跡がずれる
		source->TransitionForShaderRead(*dxCommand);
		return true;
	}

	// sourceをGameViewと同じToneMapToViewでdestへ全画面blitする、プレビューの見た目をGameViewへ合わせる用途
	bool ToneMapBlitToPreview(Engine::GraphicsCore& graphicsCore,
		const Engine::SceneExecutionContext& context,
		Engine::MultiRenderTarget* source, Engine::MultiRenderTarget* dest,
		Engine::RenderAssetLibrary& assetLibrary, Engine::PipelineStateCache& pipelineCache,
		Engine::PipelineBindingCache& srvCache, Engine::PipelineBindingCache::SlotID srcColorSlot) {

		if (!source || !dest || dest->GetColorCount() == 0 || !context.assetDatabase) {
			return false;
		}

		// GameViewのBlitToViewPassと同じビルトインToneMapマテリアルを使う
		const Engine::MaterialAsset* material = assetLibrary.LoadMaterial(Engine::BuiltinAssets::Materials::ToneMapToView);
		if (!material) {
			return false;
		}
		const Engine::MaterialPassBinding* passBinding = FindPass(*material, Engine::MaterialPassKind::Blit);
		if (!passBinding) {
			passBinding = FindPass(*material, Engine::MaterialPassKind::Fullscreen);
		}
		if (!passBinding ||
			passBinding->preferredVariant == Engine::PipelineVariantKind::Compute ||
			passBinding->preferredVariant == Engine::PipelineVariantKind::Raytracing) {
			return false;
		}

		// destの色formatを並べてPSOのRTVformatへ渡す
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
		std::vector<Engine::RenderTarget> renderTargets{};
		renderTargets.reserve(dest->GetColorCount());
		for (uint32_t i = 0; i < dest->GetColorCount(); ++i) {

			Engine::RenderTexture2D* color = dest->GetColorTexture(i);
			if (!color) {
				return false;
			}
			color->Transition(*dxCommand, D3D12_RESOURCE_STATE_RENDER_TARGET);
			renderTargets.emplace_back(color->GetRenderTarget());
		}
		dxCommand->BindRenderTargets(renderTargets, std::nullopt);
		dxCommand->SetViewportAndScissor(dest->GetWidth(), dest->GetHeight());

		dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
		commandList->SetGraphicsRootSignature(pipelineState->GetRootSignature());
		commandList->SetPipelineState(pipelineState->GetGraphicsPipeline(Engine::BlendMode::Normal));

		srvCache.Sync(*pipelineState);
		Engine::RenderTexture2D* sourceColor = source->GetColorTexture(0);
		if (!srvCache.Has(srcColorSlot) || !sourceColor) {
			return false;
		}
		Engine::RootBindingCommand::SetGraphicsSRV(commandList, srvCache.Get(srcColorSlot), 0, sourceColor->GetSRVGPUHandle());

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);

		// 表示用にdestをシェーダー読み取り状態へ戻す
		dest->TransitionForShaderRead(*dxCommand);
		return true;
	}
}

void Engine::PostProcessStackPass::Execute(GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources || !context.targetRegistry ||
		!deps_.postProcessExecutor || !deps_.postProcessTargetPool ||
		!deps_.assetLibrary || !deps_.pipelineCache) {
		return;
	}

	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
	if (!sceneFinal) {
		return;
	}

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	service.EnsureLoaded();
	const PostProcessStackRuntime& runtime = service.GetRuntime();

	if (!runtime.HasEnabledPassesForAnchor(anchor_)) {
		return;
	}

	// このアンカーに割り当てられた有効なパスだけ抽出する
	std::vector<const PostProcessStackRuntimePass*> activePasses;
	activePasses.reserve(runtime.passes.size());
	for (const auto& pass : runtime.passes) {
		if (pass.enabled && pass.material && pass.anchor == anchor_) {
			activePasses.push_back(&pass);
		}
	}
	if (activePasses.empty()) {
		return;
	}

	// SceneFinalを入力にするため、1パスだけでも一時RTを必ず経由する
	MultiRenderTarget* ping = deps_.postProcessTargetPool->Acquire(graphicsCore,
		*context.targetRegistry, kPingName, *sceneFinal);
	MultiRenderTarget* pong = nullptr;
	if (activePasses.size() > 2) {
		pong = deps_.postProcessTargetPool->Acquire(graphicsCore, *context.targetRegistry, kPongName, *sceneFinal);
	}
	if (!ping || (activePasses.size() > 2 && !pong)) {
		return;
	}

	// エディタの選択中パスを基準に、そのパス実行前後の結果をプレビューへ退避
	// 選択中パスがこのアンカーに含まれるときだけ退避先を確保する
	const UUID previewPassID = service.GetPreviewPassId();
	const bool anchorHasPreviewPass = static_cast<bool>(previewPassID) &&
		std::any_of(activePasses.begin(), activePasses.end(),
			[&](const PostProcessStackRuntimePass* p) { return p->id == previewPassID; });
	const bool capturePreview = (context.kind == RenderViewKind::Game) && anchorHasPreviewPass;
	MultiRenderTarget* previewBefore = nullptr;
	MultiRenderTarget* previewAfter = nullptr;
	bool previewCaptured = false;
	if (capturePreview) {
		previewBefore = deps_.postProcessTargetPool->Acquire(graphicsCore,
			*context.targetRegistry, "PostProcessPreviewBefore", *sceneFinal);
		previewAfter = deps_.postProcessTargetPool->Acquire(graphicsCore,
			*context.targetRegistry, "PostProcessPreviewAfter", *sceneFinal);
	}

	// 実行前にリフレクション情報をキャッシュしておく
	for (const auto* passPtr : activePasses) {

		// シェーダーリロード要求があれば、パイプラインとレイアウトキャッシュを破棄する
		if (service.TakeReloadRequest(passPtr->material)) {
			const MaterialAsset* mat = deps_.assetLibrary->LoadMaterial(passPtr->material);
			if (mat) {
				for (const auto& passBinding : mat->passes) {
					deps_.pipelineCache->InvalidateByPipelineAsset(passBinding.pipeline);
				}
			}
			deps_.postProcessExecutor->ClearParameterLayoutCache();
			service.ClearReflection(passPtr->material);
		}

		if (service.FindReflectionVars(passPtr->material) != nullptr) {
			continue;
		}
		std::vector<ShaderConstantBufferVariable> vars;
		std::vector<ShaderResourceBinding> srvs;
		std::vector<ShaderResourceBinding> samplers;
		if (deps_.postProcessExecutor->TryGetReflection(graphicsCore, *deps_.assetLibrary,
			*deps_.pipelineCache, passPtr->material, passPtr->passKind, vars, srvs, samplers)) {
			service.CacheReflection(passPtr->material, vars, srvs, samplers);
		}
	}

	// パスのsource/dest名から、対応する中間RTを引く
	auto resolveTargetByName = [&](const char* name) -> MultiRenderTarget* {
		if (!name) {
			return nullptr;
		}
		if (std::strcmp(name, kSceneColorFinal) == 0) {
			return sceneFinal;
		}
		if (std::strcmp(name, kPingName) == 0) {
			return ping;
		}
		if (std::strcmp(name, kPongName) == 0) {
			return pong;
		}
		return nullptr;
		};

	const size_t passCount = activePasses.size();
	for (size_t i = 0; i < passCount; ++i) {

		const PostProcessStackRuntimePass& pass = *activePasses[i];
		const bool isFirst = (i == 0);
		const bool isLast = (i == passCount - 1);

		const char* sourceName = nullptr;
		const char* destName = nullptr;
		if (isFirst) {
			sourceName = kSceneColorFinal;
			destName = kPingName;
		} else {
			sourceName = (i % 2 == 1) ? kPingName : kPongName;
			destName = isLast ? kSceneColorFinal : ((i % 2 == 1) ? kPongName : kPingName);
		}

		// 選択中パスなら、実行前のsource内容をbeforeへ退避する
		// GameViewと同じトーンマップを通して退避し、見た目を一致させる
		const bool isPreviewTarget = capturePreview && previewBefore && previewAfter &&
			(pass.id == previewPassID);
		if (isPreviewTarget) {
			if (!ToneMapBlitToPreview(graphicsCore, context, resolveTargetByName(sourceName), previewBefore,
				*deps_.assetLibrary, *deps_.pipelineCache, previewToneMapSRVCache_, previewToneMapSrcColorSlot_)) {
				CopyColor0Resource(graphicsCore, resolveTargetByName(sourceName), previewBefore);
			}
		}

		PostProcessExecutionDesc desc{};
		desc.material = pass.material;
		desc.passKind = pass.passKind;
		desc.source.colors = { sourceName };
		desc.dest.colors = { destName };
		desc.parameterOverrides = pass.parameterOverrides;
		desc.textureOverrides = pass.textureGuids;
		desc.samplerOverrides = pass.samplerOverrides;
		// SRVバインド名へ割り当てたGBuffer/深度などの中間RTを入力として渡す
		desc.extraSources = pass.renderTargetInputs;
		desc.dispatchMode = ComputeDispatchMode::FromDestSize;

		if (!deps_.postProcessExecutor->Execute(graphicsCore, RenderFrameRequest{},
			context, *deps_.assetLibrary, *deps_.pipelineCache, desc)) {

			// いずれかのpassが失敗したらSceneFinalの既存内容を保持して中断する
			Logger::Output(LogType::Engine, "[PostProcessStack] pass '{}' failed. Aborting stack.", pass.name);
			return;
		}

		// エディタUI用にリフレクション情報をキャッシュする
		const MaterialParameterLayout* layout = deps_.postProcessExecutor->GetLastExecutedLayout();
		if (layout) {
			service.CacheReflection(pass.material,
				layout->GetVariables(),
				deps_.postProcessExecutor->GetLastExecutedSRVBindings(),
				deps_.postProcessExecutor->GetLastExecutedSamplerBindings());
		}

		// 選択中パスなら、実行後のdest内容をafterへ退避する
		if (isPreviewTarget) {
			if (!ToneMapBlitToPreview(graphicsCore, context, resolveTargetByName(destName), previewAfter,
				*deps_.assetLibrary, *deps_.pipelineCache, previewToneMapSRVCache_, previewToneMapSrcColorSlot_)) {
				CopyColor0Resource(graphicsCore, resolveTargetByName(destName), previewAfter);
			}
			previewCaptured = true;
		}
	}

	if (passCount == 1) {
		CopyColor0Resource(graphicsCore, ping, sceneFinal);
	}

	// 選択中パスの実行前後(before/after)のSRVをサービスへ渡す
	// 退避先はCopyColor0Resource内でシェーダー読み取り状態へ遷移済み
	if (previewCaptured) {

		RenderTexture2D* beforeColor = previewBefore->GetColorTexture(0);
		RenderTexture2D* afterColor = previewAfter->GetColorTexture(0);
		PostProcessStackService::PreviewImage preview{};
		preview.beforeSrvPtr = beforeColor ? beforeColor->GetSRVGPUHandle().ptr : 0;
		preview.afterSrvPtr = afterColor ? afterColor->GetSRVGPUHandle().ptr : 0;
		preview.width = previewAfter->GetWidth();
		preview.height = previewAfter->GetHeight();
		preview.valid = (preview.beforeSrvPtr != 0 && preview.afterSrvPtr != 0);
		service.SetPreviewImage(preview);
	}
}
