#include "RenderFeaturePass.h"

//============================================================================
//	include
//============================================================================
#include "RenderFeatureResourceUtility.h"
#include "RenderFeaturePassTargets.h"
#include "RenderFeatureSelectionSession.h"
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

	class ScopedRenderFeatureGPUProfile {
	public:
		ScopedRenderFeatureGPUProfile(ID3D12GraphicsCommandList* commandList,
			std::string name) :
			commandList_(commandList) {

			Engine::GPUFrameProfiler::GetInstance().BeginPass(
				commandList_, name);
		}
		~ScopedRenderFeatureGPUProfile() {

			Engine::GPUFrameProfiler::GetInstance().EndPass(commandList_);
		}

		ScopedRenderFeatureGPUProfile(
			const ScopedRenderFeatureGPUProfile&) = delete;
		ScopedRenderFeatureGPUProfile& operator=(
			const ScopedRenderFeatureGPUProfile&) = delete;
	private:
		ID3D12GraphicsCommandList* commandList_ = nullptr;
	};

	std::string MakePassProfileName(Engine::RenderViewKind kind,
		std::string_view passName) {

		return std::string(Engine::EnumAdapter<Engine::RenderViewKind>::
			ToStringView(kind)) + "/RenderFeature/" + std::string(passName);
	}

	std::string MakeSelectionEffectAlias(Engine::UUID groupID) {

		return "RenderFeatureSelectionEffect_" +
			std::to_string(groupID.value);
	}

}

//============================================================================
//	RenderFeaturePass classMethods
//============================================================================
void Engine::RenderFeaturePass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets,
	SceneExecutionContext& context) {

	if (!context.resources || !context.targetRegistry ||
		!deps_.assetLibrary || !deps_.pipelineCache ||
		!deps_.postProcessExecutor || !deps_.postProcessTargetPool ||
		!deps_.rayTracingExecutor || !deps_.raytracingPipelineCache) {

		return;
	}

	// トーンマッピング後はHDRシーンではなくUIと同じビューを入出力に使う
	const bool afterToneMap = anchor_ == RenderFeatureAnchor::AfterToneMap;
	const std::string sceneColorAlias = afterToneMap ?
		"View" : RenderTargetNames::kSceneColorFinal;
	MultiRenderTarget* sceneFinal = afterToneMap ?
		context.defaultSurface : context.resources->GetSceneFinal();
	if (!sceneFinal || !sceneFinal->GetColorTexture(0)) {
		return;
	}

	RenderFeatureProfileService& service =
		RenderFeatureProfileService::GetInstance();
	service.EnsureLoaded();
	const RenderFeatureExecutionPlan plan =
		service.GetRuntime().BuildPlan(anchor_, context.kind);
	if (!plan.IsValid()) {
		if (lastDiagnostic_ != plan.diagnostic) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レンダー機能] {}", plan.diagnostic);
			lastDiagnostic_ = plan.diagnostic;
		}
		return;
	}
	const DXGI_FORMAT sceneFormat =
		sceneFinal->GetColorTexture(0)->GetFormat();
	const GraphicsRuntimeFeatures& runtimeFeatures = graphicsCore.
		GetDXObject().GetFeatureController().GetRuntimeFeatures();
	std::unordered_map<uint64_t, float> resolutionScales{};
	std::unordered_map<uint64_t, bool> raytracingChains{};
	RenderFeatureSelectionSession selection{};
	for (size_t nodeIndex = 0; nodeIndex < plan.nodes.size(); ++nodeIndex) {

		const RenderFeaturePlanNode& node = plan.nodes[nodeIndex];

		if (!node.pass) {
			continue;
		}
		const RenderFeatureHierarchyItem* selectionGroup =
			node.selectionGroup;
		const bool selectionBegin = node.selectionBegin;
		const bool selectionEnd = node.selectionEnd;
		if (selectionBegin && !selection.Begin(graphicsCore, context, deps_, service.GetRuntime(),
			passBuckets, selectionGroup, *sceneFinal, sceneFormat)) {
			return;
		}
		if (selection.skippedSelection && selection.skippedSelection == selectionGroup) {
			if (selectionEnd) {
				selection.skippedSelection = nullptr;
				selection.selectionTarget = nullptr;
				selection.selectionAlias.clear();
			}
			continue;
		}
		const RenderFeaturePassSettings& pass = *node.pass;
		bool raytracingChain =
			pass.type == RenderFeaturePassType::RayTracing;
		const auto extendRaytracingChain = [&](UUID dependency) {

			if (!dependency) {
				return;
			}
			const auto found = raytracingChains.find(dependency.value);
			raytracingChain |= found != raytracingChains.end() &&
				found->second;
		};
		extendRaytracingChain(node.source.pass);
		for (const auto& [shaderResource, input] : pass.passInputs) {

			extendRaytracingChain(input.pass);
		}
		raytracingChains[pass.id.value] = raytracingChain;
		const std::string profileName = MakePassProfileName(
			context.kind, pass.name);
		ScopedRenderFeatureGPUProfile gpuProfile{
			graphicsCore.GetDXObject().GetDxCommand()->GetCommandList(),
			profileName };

		float dependencyScale = 1.0f;
		if (node.source.pass) {
			const auto sourceScale = resolutionScales.find(
				node.source.pass.value);
			if (sourceScale != resolutionScales.end()) {
				dependencyScale = sourceScale->second;
			}
		}
		float adaptiveScale = 1.0f;
		if (pass.adaptiveResolution) {
			adaptiveScale = temporalState_.UpdateResolution(pass, MakeStateKey(context.kind, pass.id), profileName);
		}
		// 依存チェーン全体で同じ動的解像度係数を使い、各出力の基準倍率へ重ねて適用する
		const float outputScale = pass.adaptiveResolution ?
			adaptiveScale : dependencyScale;
		resolutionScales[pass.id.value] = outputScale;
		RenderFeaturePassTargets targets{};
		if (!targets.Resolve(graphicsCore, context, *deps_.postProcessTargetPool, temporalState_,
			pass, *sceneFinal, sceneFormat, outputScale, raytracingChain, runtimeFeatures, service.GetRuntimeGeneration())) {
			return;
		}

		MultiRenderTarget* primaryTarget =
			ResolveOutputTarget(*context.targetRegistry, targets.primaryReference);
		const bool isolatedSource = selectionBegin && selectionGroup &&
			selectionGroup->selection.mode ==
				RenderFeatureSelectionMode::IsolatedLayer;
		MultiRenderTarget* sourceTarget = isolatedSource ? selection.selectionTarget :
			(node.source.pass ? ResolveOutputTarget(
				*context.targetRegistry, node.source) : sceneFinal);
		if (!primaryTarget || !sourceTarget) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レンダー機能] グラフリソースを解決できません パス={}", pass.name);
			return;
		}
		const std::string sourceAlias = isolatedSource ? selection.selectionAlias :
			(node.source.pass ? MakeOutputAlias(node.source) :
				sceneColorAlias);
		const RenderFeaturePassRuntimeOverride* runtimeOverride =
			RenderFeatureRuntimeOverrides::GetInstance().Find(pass.id);
		const bool rayTracingUnavailable =
			pass.type == RenderFeaturePassType::RayTracing &&
			(!graphicsCore.GetDXObject().ShouldUseDispatchRays() ||
				!context.raytracing.tlasResource);
		const bool rayTracingMaterialsPending =
			pass.type == RenderFeaturePassType::RayTracing &&
			!context.raytracing.materialTexturesReady;
		const bool useSelectionComposite =
			selectionGroup && selectionEnd;
		MultiRenderTarget* executionPrimaryTarget = primaryTarget;
		std::string executionPrimaryAlias = MakeOutputAlias(targets.primaryReference);
		if (useSelectionComposite) {

			PostProcessTemporaryTargetDesc desc{};
			desc.name = MakeSelectionEffectAlias(selectionGroup->id);
			desc.format = sceneFormat;
			executionPrimaryTarget = deps_.postProcessTargetPool->Acquire(
				graphicsCore, *context.targetRegistry, desc, *sceneFinal);
			if (!executionPrimaryTarget ||
				!executionPrimaryTarget->GetColorTexture(0)) {

				Logger::Output(LogType::Engine, spdlog::level::err,
					"[レンダー機能] 選択効果出力を作成できません パス={}",
					pass.name);
				return;
			}
			executionPrimaryAlias = desc.name;
			targets.outputTargets[targets.primaryOutput.shaderResource] =
				executionPrimaryTarget;
		}
		const bool bypassPass =
			(runtimeOverride && runtimeOverride->enabled == false) ||
			rayTracingUnavailable || rayTracingMaterialsPending;
		if (bypassPass) {

			ClearOutputTargets(graphicsCore, targets.outputTargets);
			// 無効化した通常パスは同一形式なら入力をそのまま引き継ぐ
			if (!rayTracingUnavailable && !rayTracingMaterialsPending &&
				CanCopyColor(sourceTarget, executionPrimaryTarget) &&
				!MultiRenderTargetCopy::CopyColor0Resource(
					graphicsCore, sourceTarget, executionPrimaryTarget)) {

				Logger::Output(LogType::Engine, spdlog::level::err,
					"[レンダー機能] パスのバイパスに失敗しました パス={}",
					pass.name);
				return;
			}
			if (!useSelectionComposite) {
				continue;
			}
		}

		if (!bypassPass) {
			std::unordered_map<std::string, std::string> inputs{};
			ApplyDefaultSceneInputs(inputs);
			for (const auto& [resourceName, targetName] : pass.sceneInputs) {
				inputs.insert_or_assign(resourceName, targetName);
			}
			for (const auto& [resourceName, targetName] : targets.historyInputs) {
				inputs.insert_or_assign(resourceName, targetName);
			}
			inputs[PostProcessBindingNames::kSourceColor] = sourceAlias;
			for (const auto& [name, reference] : pass.passInputs) {

				inputs[name] = reference.pass ?
					MakeOutputAlias(reference) : sceneColorAlias;
			}
			if (pass.type == RenderFeaturePassType::Compute) {
				PostProcessExecutionDesc desc{};
				desc.material = pass.material;
				desc.passKind = pass.materialPass;
				desc.source.colors = { sourceAlias };
				desc.dest.colors = { executionPrimaryAlias };
				desc.extraSources = inputs;
				desc.parameterOverrides = pass.parameterOverrides;
				if (runtimeOverride) {
					desc.parameterOverrides.MergeFrom(
						runtimeOverride->parameters);
				}
				desc.textureOverrides = pass.textureOverrides;
				if (runtimeOverride) {
					for (const auto& [name, texture] :
						runtimeOverride->textureOverrides) {

						desc.textureOverrides[name] = texture;
					}
				}
				desc.samplerOverrides = pass.samplerOverrides;
				desc.dispatchMode = ComputeDispatchMode::FromDestSize;
				for (const auto& [resourceName, target] : targets.outputTargets) {

					if (target == executionPrimaryTarget) {
						desc.outputTargets[resourceName] = executionPrimaryAlias;
						continue;
					}
					const auto output = std::find_if(targets.defaultOutputs.begin(),
						targets.defaultOutputs.end(),
						[&](const RenderFeatureOutputSettings& settings) {

							return settings.shaderResource == resourceName;
						});
					if (output == targets.defaultOutputs.end()) {
						continue;
					}
					desc.outputTargets[resourceName] = MakeOutputAlias(
						RenderFeatureOutputReference{
							.pass = pass.id,
							.output = output->name,
						});
				}
				if (!deps_.postProcessExecutor->Execute(graphicsCore,
					RenderFrameRequest{}, context, *deps_.assetLibrary,
					*deps_.pipelineCache, desc)) {

					Logger::Output(LogType::Engine, spdlog::level::err,
						"[レンダー機能] Computeパスに失敗しました パス={}",
						pass.name);
					return;
				}
				const MaterialParameterLayout* layout =
					deps_.postProcessExecutor->GetLastExecutedLayout();
				if (layout) {
					service.CacheReflection(pass.material, pass.materialPass,
						layout->GetVariables(),
						deps_.postProcessExecutor->GetLastExecutedSRVBindings(),
						deps_.postProcessExecutor->GetLastExecutedSamplerBindings());
				}
			} else {
				RayTracingExecutionResources resources{};
				resources.inputs = std::move(inputs);
				resources.dispatchTarget =
					executionPrimaryTarget->GetColorTexture(0);
				for (const auto& [resourceName, target] : targets.outputTargets) {

					resources.outputs[resourceName] = target->GetColorTexture(0);
				}
				if (!deps_.rayTracingExecutor->Execute(graphicsCore, context,
					*deps_.assetLibrary, *deps_.raytracingPipelineCache,
					pass, resources, runtimeOverride)) {

					return;
				}
				if (const ShaderReflectionInfo* reflection =
					deps_.rayTracingExecutor->GetLastReflection()) {

					std::vector<ShaderConstantBufferVariable> variables{};
					std::vector<ShaderResourceBinding> resourceBindings{};
					std::vector<ShaderResourceBinding> samplers{};
					for (const ShaderConstantBufferInfo& buffer :
						reflection->constantBuffers) {

						if (buffer.name == "RayTracingParameters") {
							variables = buffer.variables;
							break;
						}
					}
					for (const ShaderResourceBinding& binding :
						reflection->resources) {

						(binding.kind == ShaderBindingKind::Sampler ?
							samplers : resourceBindings).emplace_back(binding);
					}
					service.CacheReflection(pass.material, pass.materialPass,
						variables, resourceBindings, samplers);
				}
			}
		}
		for (const std::string& historyKey : targets.writtenHistoryKeys) {
			temporalState_.MarkWritten(historyKey);
		}

		if (useSelectionComposite && !selection.Composite(graphicsCore, context, deps_, selectionGroup,
			sceneColorAlias, executionPrimaryAlias, targets.primaryReference, primaryTarget, sceneFinal, pass.name)) {
			return;
		}
	}

	if (plan.sceneColorOutput.pass) {
		MultiRenderTarget* output = ResolveOutputTarget(
			*context.targetRegistry, plan.sceneColorOutput);
		if (!output || !output->GetColorTexture(0)) {

			constexpr std::string_view diagnostic =
				"SceneColorOutputMissing";
			if (lastDiagnostic_ != diagnostic) {
				Logger::Output(LogType::Engine, spdlog::level::err,
					"[レンダー機能] SceneColor出力が生成されていません "
					"PassID={} 実行ノード数={}",
					plan.sceneColorOutput.pass.value, plan.nodes.size());
				lastDiagnostic_ = diagnostic;
			}
			return;
		}
		if (!CanCopyColor(output, sceneFinal)) {

			constexpr std::string_view diagnostic =
				"SceneColorOutputMismatch";
			if (lastDiagnostic_ != diagnostic) {
				Logger::Output(LogType::Engine, spdlog::level::err,
					"[レンダー機能] SceneColor出力のサイズまたは形式が一致しません "
					"出力={}x{} 形式={} View={}x{} 形式={}",
					output->GetWidth(), output->GetHeight(),
					EnumAdapter<DXGI_FORMAT>::ToString(
						output->GetColorTexture(0)->GetFormat()),
					sceneFinal->GetWidth(), sceneFinal->GetHeight(),
					EnumAdapter<DXGI_FORMAT>::ToString(
						sceneFinal->GetColorTexture(0)->GetFormat()));
				lastDiagnostic_ = diagnostic;
			}
			return;
		}
		if (!MultiRenderTargetCopy::CopyColor0Resource(
			graphicsCore, output, sceneFinal)) {

			constexpr std::string_view diagnostic =
				"SceneColorOutputCopyFailed";
			if (lastDiagnostic_ != diagnostic) {
				Logger::Output(LogType::Engine, spdlog::level::err,
					"[レンダー機能] SceneColor出力のコピーに失敗しました");
				lastDiagnostic_ = diagnostic;
			}
			return;
		}
		sceneFinal->TransitionForShaderRead(
			*graphicsCore.GetDXObject().GetDxCommand());
	}
	lastDiagnostic_.clear();
}
