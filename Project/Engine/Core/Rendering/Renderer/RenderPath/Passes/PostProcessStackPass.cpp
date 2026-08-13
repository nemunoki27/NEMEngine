#include "PostProcessStackPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessExecutor.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessBindingNames.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessTemporaryTargetPool.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackService.h>
#include <Engine/Core/Rendering/PostProcess/Color/ColorPipelineProcessor.h>
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
#include <array>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>

//============================================================================
//	PostProcessStackPass classMethods
//============================================================================

namespace {

	constexpr const char* kSceneColorFinal = Engine::RenderTargetNames::kSceneColorFinal;

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

}

void Engine::PostProcessStackPass::Execute(GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources || !context.targetRegistry ||
		!deps_.postProcessExecutor || !deps_.postProcessTargetPool ||
		!deps_.assetLibrary || !deps_.pipelineCache ||
		!deps_.colorPipelineProcessor) {
		return;
	}

	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
	if (!sceneFinal) {
		return;
	}

	PostProcessStackService& service = PostProcessStackService::GetInstance();
	service.EnsureLoaded();
	const PostProcessStackRuntime& runtime = service.GetRuntime();

	const PostProcessGraphPlan graphPlan =
		runtime.BuildGraphPlan(anchor_);
	if (graphPlan.nodes.empty()) {
		if (!graphPlan.diagnostic.empty() &&
			graphPlan.diagnostic != lastGraphDiagnostic_) {

			Logger::Output(
				LogType::Engine,
				"[PostProcessGraph] {}",
				graphPlan.diagnostic);
		}
		lastGraphDiagnostic_ =
			graphPlan.diagnostic;
		return;
	}
	lastGraphDiagnostic_.clear();

	// エディタの選択中パスを基準に、そのパス実行前後の結果をプレビューへ退避
	const UUID previewPassID = service.GetPreviewPassId();
	const bool anchorHasPreviewPass = static_cast<bool>(previewPassID) &&
		std::any_of(
			graphPlan.nodes.begin(),
			graphPlan.nodes.end(),
			[&](const PostProcessGraphPlanNode& node) {
				return node.pass &&
					node.pass->id == previewPassID;
			});
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

	// 実行前に使用パスだけのリフレクション情報をキャッシュする
	for (const PostProcessGraphPlanNode& node :
		graphPlan.nodes) {

		const PostProcessStackRuntimePass* passPtr =
			node.pass;
		if (!passPtr) {
			continue;
		}

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

	struct GraphOutputResource {

		MultiRenderTarget* target = nullptr;
		std::string alias;
		uint32_t slot = 0;
	};
	struct GraphTargetSlot {

		MultiRenderTarget* target = nullptr;
		std::string alias;
		UUID owner{};
	};

	std::unordered_map<uint64_t, uint32_t>
		remainingUses{};
	for (const PostProcessGraphPlanNode& node :
		graphPlan.nodes) {

		if (node.sourcePass) {
			++remainingUses[node.sourcePass.value];
		}
		for (const auto& [name, source] :
			node.pass->passInputs) {

			(void)name;
			if (source) {
				++remainingUses[source.value];
			}
		}
	}
	// 最終出力は全ノード実行後のSceneFinalコピーまで保持する
	++remainingUses[graphPlan.outputPass.value];

	std::vector<GraphTargetSlot> slots{};
	std::unordered_map<uint64_t,
		GraphOutputResource> outputs{};
	MultiRenderTarget* maskEffectTarget = nullptr;
	const std::string maskEffectAlias =
		"PostProcessMaskEffect" +
		std::to_string(
			static_cast<uint32_t>(anchor_));

	for (const PostProcessGraphPlanNode& node :
		graphPlan.nodes) {

		if (!node.pass) {
			continue;
		}
		const PostProcessStackRuntimePass& pass =
			*node.pass;
		const GraphOutputResource* sourceOutput =
			nullptr;
		if (node.sourcePass) {
			const auto found =
				outputs.find(node.sourcePass.value);
			if (found == outputs.end()) {
				return;
			}
			sourceOutput = &found->second;
		}
		MultiRenderTarget* sourceTarget =
			sourceOutput ?
			sourceOutput->target : sceneFinal;
		const std::string sourceName =
			sourceOutput ?
			sourceOutput->alias : kSceneColorFinal;

		std::unordered_set<MultiRenderTarget*>
			inputTargets{ sourceTarget };
		for (const auto& [name, source] :
			pass.passInputs) {

			(void)name;
			const auto found =
				outputs.find(source.value);
			if (found != outputs.end()) {
				inputTargets.insert(
					found->second.target);
			}
		}

		uint32_t slotIndex =
			static_cast<uint32_t>(slots.size());
		for (uint32_t index = 0;
			index < slots.size(); ++index) {

			const GraphTargetSlot& slot =
				slots[index];
			if (slot.owner &&
				remainingUses[slot.owner.value] != 0) {
				continue;
			}
			if (inputTargets.contains(slot.target)) {
				continue;
			}
			slotIndex = index;
			break;
		}
		if (slotIndex == slots.size()) {
			const std::string alias =
				"PostProcessGraphSlot" +
				std::to_string(
					static_cast<uint32_t>(anchor_)) +
				"_" + std::to_string(slotIndex);
			MultiRenderTarget* target =
				deps_.postProcessTargetPool->Acquire(
					graphicsCore,
					*context.targetRegistry,
					alias, *sceneFinal);
			if (!target) {
				return;
			}
			slots.emplace_back(
				GraphTargetSlot{
					.target = target,
					.alias = alias,
				});
		}
		GraphTargetSlot& outputSlot =
			slots[slotIndex];
		outputSlot.owner = pass.id;
		outputs[pass.id.value] =
			GraphOutputResource{
				.target = outputSlot.target,
				.alias = outputSlot.alias,
				.slot = slotIndex,
			};

		// 選択中パスなら、実行前のsource内容をbeforeへ退避する
		const bool isPreviewTarget = capturePreview && previewBefore && previewAfter &&
			(pass.id == previewPassID);
		if (isPreviewTarget) {
			if (!deps_.colorPipelineProcessor->ToneMap(graphicsCore, context,
				sourceTarget, previewBefore, *deps_.assetLibrary,
				*deps_.pipelineCache, service.GetSettings().colorPipeline, false)) {
				CopyColor0Resource(
					graphicsCore,
					sourceTarget, previewBefore);
			}
		}

		PostProcessExecutionDesc desc{};
		desc.material = pass.material;
		desc.passKind = pass.passKind;
		desc.source.colors = { sourceName };
		const bool useTargetMask =
			pass.targetMask != 0u;
		if (useTargetMask && !maskEffectTarget) {
			maskEffectTarget =
				deps_.postProcessTargetPool->Acquire(
					graphicsCore,
					*context.targetRegistry,
					maskEffectAlias, *sceneFinal);
			if (!maskEffectTarget) {
				return;
			}
		}
		desc.dest.colors = {
			useTargetMask ?
			maskEffectAlias : outputSlot.alias
		};
		desc.parameterOverrides = pass.parameterOverrides;
		desc.textureOverrides = pass.textureGuids;
		desc.samplerOverrides = pass.samplerOverrides;
		// SRVバインド名へ割り当てたGBuffer/深度などの中間RTを入力として渡す
		desc.extraSources = pass.renderTargetInputs;
		for (const auto& [name, source] :
			pass.passInputs) {

			const auto found =
				outputs.find(source.value);
			if (found != outputs.end()) {
				desc.extraSources[name] =
					found->second.alias;
			}
		}
		desc.dispatchMode = ComputeDispatchMode::FromDestSize;

		if (!deps_.postProcessExecutor->Execute(graphicsCore, RenderFrameRequest{},
			context, *deps_.assetLibrary, *deps_.pipelineCache, desc)) {

			// いずれかのpassが失敗したらSceneFinalの既存内容を保持して中断する
			Logger::Output(LogType::Engine, "[PostProcessStack] pass '{}' failed. Aborting stack.", pass.name);
			return;
		}

		// 合成パスで上書きされる前に元パスのreflectionを退避する
		const MaterialParameterLayout* layout =
			deps_.postProcessExecutor->
			GetLastExecutedLayout();
		if (layout) {
			service.CacheReflection(
				pass.material,
				layout->GetVariables(),
				deps_.postProcessExecutor->
				GetLastExecutedSRVBindings(),
				deps_.postProcessExecutor->
				GetLastExecutedSamplerBindings());
		}

		if (useTargetMask) {
			PostProcessExecutionDesc composite{};
			composite.material =
				BuiltinAssets::Materials::
				PostProcessMaskComposite;
			composite.passKind =
				MaterialPassKind::PostProcess;
			composite.source.colors = { sourceName };
			composite.dest.colors = {
				outputSlot.alias
			};
			composite.extraSources[
				PostProcessBindingNames::
				kEffectColor] =
				maskEffectAlias;
			composite.extraSources[
				PostProcessBindingNames::
				kSourceFlags] =
				RenderTargetNames::
				kSceneFlagsMain;

			MaterialParameterValue targetMask{};
			targetMask.value =
				pass.targetMask &
				kRenderingLayerMaskBits;
			composite.parameterOverrides.Set(
				MaterialParameterIDs::TargetMask,
				MaterialParameterNames::TargetMask,
				MaterialParameterSemantic::None,
				targetMask);
			if (!deps_.postProcessExecutor->Execute(
				graphicsCore,
				RenderFrameRequest{}, context,
				*deps_.assetLibrary,
				*deps_.pipelineCache,
				composite)) {

				Logger::Output(
					LogType::Engine,
					"[PostProcessGraph] mask composite failed. pass={}",
					pass.name);
				return;
			}
		}

		// 選択中パスなら、実行後のdest内容をafterへ退避する
		if (isPreviewTarget) {
			if (!deps_.colorPipelineProcessor->ToneMap(graphicsCore, context,
				outputSlot.target, previewAfter, *deps_.assetLibrary,
				*deps_.pipelineCache, service.GetSettings().colorPipeline, false)) {
				CopyColor0Resource(
					graphicsCore,
					outputSlot.target, previewAfter);
			}
			previewCaptured = true;
		}

		// このパスが読み終えた依存出力は次ノードから再利用可能にする
		if (node.sourcePass) {
			--remainingUses[
				node.sourcePass.value];
		}
		for (const auto& [name, source] :
			pass.passInputs) {

			(void)name;
			if (source) {
				--remainingUses[source.value];
			}
		}
	}

	const auto finalOutput =
		outputs.find(graphPlan.outputPass.value);
	if (finalOutput == outputs.end() ||
		!CopyColor0Resource(
			graphicsCore,
			finalOutput->second.target,
			sceneFinal)) {
		return;
	}
	--remainingUses[graphPlan.outputPass.value];

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
