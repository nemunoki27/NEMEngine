#include "RenderFeatureSelectionSession.h"

//============================================================================
//	include
//============================================================================
#include "RenderFeatureResourceUtility.h"
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessBindingNames.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessExecutor.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessTemporaryTargetPool.h>
#include <Engine/Core/Rendering/Raytracing/RayTracingExecutor.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineStateCache.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTargetCopyUtility.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphBindingNames.h>

// c++
#include <algorithm>
#include <string_view>

using namespace Engine::RenderFeatureResourceUtility;

namespace {

	std::string MakeSelectionOutputAlias(Engine::UUID groupID) {

		return "RenderFeatureSelection_" + std::to_string(groupID.value);
	}

	void CollectSelectionItems(
		const Engine::RenderPassPhaseBuckets& passBuckets,
		const Engine::RenderFeatureSelectionSettings& selection,
		std::vector<const Engine::RenderItem*>& outItems) {

		outItems.clear();
		for (uint32_t index = 0;
			index < static_cast<uint32_t>(Engine::RenderPhase::Count);
			++index) {

			const Engine::RenderPhase phase =
				static_cast<Engine::RenderPhase>(index);
			if ((selection.phaseMask &
				Engine::MakeRenderFeaturePhaseMask(phase)) == 0u) {

				continue;
			}
			for (const Engine::RenderItem* item :
				passBuckets.Get(phase).items) {

				if (item && Engine::MatchesRenderFeatureSelection(
					*item, selection)) {
					outItems.emplace_back(item);
				}
			}
		}
	}
}

bool Engine::RenderFeatureSelectionSession::Begin(GraphicsCore& graphicsCore, SceneExecutionContext& context, const RenderPipelineDeps& deps,
			const RenderFeatureProfileRuntime& runtime, const RenderPassPhaseBuckets& passBuckets,
			const RenderFeatureHierarchyItem* selectionGroup, MultiRenderTarget& sceneFinal, DXGI_FORMAT sceneFormat) {

	const RenderFeatureSelectionSettings& selection =
		selectionGroup->selection;
	const bool groupEnabled = runtime.IsGroupEnabled(
		*selectionGroup);
	if (groupEnabled) {
		CollectSelectionItems(passBuckets, selection, selectionItems);
	} else {
		selectionItems.clear();
	}
	if (selectionItems.empty()) {
		skippedSelection = selectionGroup;
	}
	if (!skippedSelection) {
		PostProcessTemporaryTargetDesc desc{};
		desc.name = MakeSelectionOutputAlias(selectionGroup->id);
		desc.format = sceneFormat;
		selectionTarget = deps.postProcessTargetPool->Acquire(
			graphicsCore, *context.targetRegistry, desc, sceneFinal);
		if (!selectionTarget ||
			!selectionTarget->GetColorTexture(0)) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レンダー機能] 選択描画先を作成できません グループ={}",
				selectionGroup->name);
			return false;
		}
		selectionAlias = desc.name;
		context.targetRegistry->Register(selectionAlias,
			selectionTarget, { selectionAlias }, std::nullopt);
		selectionTarget->TransitionForRender(
			*graphicsCore.GetDXObject().GetDxCommand());
		selectionTarget->Clear(
			*graphicsCore.GetDXObject().GetDxCommand(),
			MultiRenderTargetClearDesc{
				.clearColor = true,
				.clearColorValue = Color4::Black(0.0f),
			});

		for (uint32_t phaseIndex = 0;
			phaseIndex < static_cast<uint32_t>(RenderPhase::Count);
			++phaseIndex) {

			const RenderPhase phase =
				static_cast<RenderPhase>(phaseIndex);
			// Opaqueは既存GBufferのRendering Layerから抽出する
			if (phase == RenderPhase::Opaque) {
				continue;
			}
			if ((selection.phaseMask &
				MakeRenderFeaturePhaseMask(phase)) == 0u) {

				continue;
			}
			phaseItems.clear();
			for (const RenderItem* item : selectionItems) {
				if (item && item->renderPhase == phase) {
					phaseItems.emplace_back(item);
				}
			}
			if (phaseItems.empty()) {
				continue;
			}
			DepthTexture2D* depth = phase ==
				RenderPhase::PostProcessUI ? nullptr :
					context.resources->GetSceneMain()->
						GetDepthTexture();
			const MaterialPassKind passKind = phase ==
				RenderPhase::Transparent ?
					MaterialPassKind::Transparent :
					MaterialPassKind::Draw;
			RenderPassExecutionHelper::Execute(graphicsCore,
				context, phaseItems, deps, RenderPassSurfaceBinding{
					.colorSurface = selectionTarget,
					.depthOverride = depth,
				}, passKind);
		}
		selectionTarget->TransitionForShaderRead(
			*graphicsCore.GetDXObject().GetDxCommand());
	}

	return true;
}

bool Engine::RenderFeatureSelectionSession::Composite(GraphicsCore& graphicsCore, SceneExecutionContext& context, const RenderPipelineDeps& deps,
			const RenderFeatureHierarchyItem* selectionGroup, const std::string& sceneColorAlias,
			const std::string& executionPrimaryAlias, const RenderFeatureOutputReference& primaryReference,
			MultiRenderTarget* primaryTarget, MultiRenderTarget* sceneFinal, const std::string& passName) {

	PostProcessExecutionDesc composite{};
	composite.material =
		BuiltinAssets::Materials::PostProcessMaskComposite;
	composite.passKind = MaterialPassKind::PostProcess;
	composite.source.colors = { sceneColorAlias };
	composite.dest.colors = { MakeOutputAlias(primaryReference) };
	composite.extraSources[PostProcessBindingNames::kEffectColor] =
		executionPrimaryAlias;
	composite.extraSources[PostProcessBindingNames::kSelectionMask] =
		selectionAlias;
	composite.extraSources[PostProcessBindingNames::kSourceFlags] =
		RenderTargetNames::kSceneFlagsMain;

	MaterialParameterValue selectionMode{};
	selectionMode.value = static_cast<uint32_t>(
		selectionGroup->selection.mode);
	composite.parameterOverrides.Set(
		MaterialParameterIDs::SelectionMode,
		MaterialParameterNames::SelectionMode,
		MaterialParameterSemantic::None, selectionMode);
	MaterialParameterValue compositeMode{};
	compositeMode.value = static_cast<uint32_t>(
		selectionGroup->selection.compositeMode);
	composite.parameterOverrides.Set(
		MaterialParameterIDs::CompositeMode,
		MaterialParameterNames::CompositeMode,
		MaterialParameterSemantic::None, compositeMode);
	MaterialParameterValue renderingLayerMask{};
	renderingLayerMask.value = selectionGroup->selection.
		renderingLayerMask & kRenderingLayerMaskBits;
	composite.parameterOverrides.Set(
		MaterialParameterIDs::RenderingLayerMask,
		MaterialParameterNames::RenderingLayerMask,
		MaterialParameterSemantic::None, renderingLayerMask);
	if (!deps.postProcessExecutor->Execute(graphicsCore,
		RenderFrameRequest{}, context, *deps.assetLibrary,
		*deps.pipelineCache, composite)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レンダー機能] 選択描画の合成に失敗しました パス={}",
			passName);
		return false;
	}
	if (!MultiRenderTargetCopy::CopyColor0Resource(
		graphicsCore, primaryTarget, sceneFinal)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レンダー機能] 選択描画をScene Colorへ反映できません グループ={}",
			selectionGroup->name);
		return false;
	}
	selectionTarget = nullptr;
	selectionAlias.clear();

	return true;
}
