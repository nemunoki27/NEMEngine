#include "RenderFeaturePass.h"

//============================================================================
//	include
//============================================================================
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

namespace {

	constexpr std::string_view kDefaultOutputName = "Color";

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

	std::string MakeStateKey(Engine::RenderViewKind kind,
		Engine::UUID passID, std::string_view output = {}) {

		return std::to_string(static_cast<uint32_t>(kind)) + "_" +
			std::to_string(passID.value) + "_" + std::string(output);
	}

	DXGI_FORMAT ToDXGIFormat(
		Engine::RenderFeatureTextureFormat format,
		DXGI_FORMAT inheritedFormat) {

		switch (format) {
		case Engine::RenderFeatureTextureFormat::Inherit:
			return inheritedFormat;
		case Engine::RenderFeatureTextureFormat::R8_UNORM:
			return DXGI_FORMAT_R8_UNORM;
		case Engine::RenderFeatureTextureFormat::R16_FLOAT:
			return DXGI_FORMAT_R16_FLOAT;
		case Engine::RenderFeatureTextureFormat::RG16_FLOAT:
			return DXGI_FORMAT_R16G16_FLOAT;
		case Engine::RenderFeatureTextureFormat::RGBA16_FLOAT:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case Engine::RenderFeatureTextureFormat::R32_FLOAT:
			return DXGI_FORMAT_R32_FLOAT;
		case Engine::RenderFeatureTextureFormat::RG32_FLOAT:
			return DXGI_FORMAT_R32G32_FLOAT;
		case Engine::RenderFeatureTextureFormat::RGBA32_FLOAT:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		}
		return inheritedFormat;
	}

	std::string MakeOutputAlias(
		const Engine::RenderFeatureOutputReference& reference) {

		if (!reference.pass) {
			return Engine::RenderTargetNames::kSceneColorFinal;
		}
		return "RenderFeature_" + std::to_string(reference.pass.value) +
			"_" + (reference.output.empty() ?
				std::string(kDefaultOutputName) : reference.output);
	}

	std::string MakeSelectionOutputAlias(Engine::UUID groupID) {

		return "RenderFeatureSelection_" + std::to_string(groupID.value);
	}

	std::string MakeSelectionEffectAlias(Engine::UUID groupID) {

		return "RenderFeatureSelectionEffect_" +
			std::to_string(groupID.value);
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

	Engine::RenderFeatureOutputSettings GetPrimaryOutput(
		const Engine::RenderFeaturePassSettings& pass) {

		if (!pass.outputs.empty()) {
			return pass.outputs.front();
		}
		return Engine::RenderFeatureOutputSettings{};
	}

	Engine::MultiRenderTarget* ResolveOutputTarget(
		Engine::RenderTargetRegistry& registry,
		const Engine::RenderFeatureOutputReference& reference) {

		return registry.Find(MakeOutputAlias(reference));
	}

	void ApplyDefaultSceneInputs(
		std::unordered_map<std::string, std::string>& inputs) {

		const auto setDefault = [&](std::string_view resource,
			std::string_view target) {

			inputs.try_emplace(std::string(resource), std::string(target));
		};
		setDefault(Engine::PostProcessBindingNames::kSourceDepth,
			Engine::RenderTargetNames::kSceneDepth);
		setDefault(Engine::PostProcessBindingNames::kSourceAlbedo,
			Engine::RenderTargetNames::kSceneColorMain);
		setDefault("gSourceNormal",
			Engine::RenderTargetNames::kSceneNormalMain);
		setDefault("gSourcePosition",
			Engine::RenderTargetNames::kScenePositionMain);
		setDefault("gSourceMaterial",
			Engine::RenderTargetNames::kSceneMaterialMain);
		setDefault(Engine::PostProcessBindingNames::kSourceFlags,
			Engine::RenderTargetNames::kSceneFlagsMain);
		setDefault(Engine::PostProcessBindingNames::kSourceMotion,
			Engine::RenderTargetNames::kSceneMotionMain);
		setDefault(Engine::ShaderGraphBindingNames::kSceneColor,
			Engine::RenderTargetNames::kSceneColorOpaque);
		setDefault(Engine::ShaderGraphBindingNames::kSceneDepth,
			Engine::RenderTargetNames::kSceneDepth);
		setDefault(Engine::ShaderGraphBindingNames::kSceneNormal,
			Engine::RenderTargetNames::kSceneNormalMain);
		setDefault(Engine::ShaderGraphBindingNames::kScenePosition,
			Engine::RenderTargetNames::kScenePositionMain);
		setDefault(Engine::ShaderGraphBindingNames::kSceneMaterial,
			Engine::RenderTargetNames::kSceneMaterialMain);
		setDefault(Engine::ShaderGraphBindingNames::kSceneEmissive,
			Engine::RenderTargetNames::kSceneEmissiveMain);
		setDefault(Engine::ShaderGraphBindingNames::kSceneFlags,
			Engine::RenderTargetNames::kSceneFlagsMain);
	}

	void ClearOutputTargets(Engine::GraphicsCore& graphicsCore,
		const std::unordered_map<std::string,
			Engine::MultiRenderTarget*>& outputTargets) {

		for (const auto& [name, target] : outputTargets) {
			(void)name;
			if (!target || !target->GetColorTexture(0)) {
				continue;
			}
			target->TransitionForRender(
				*graphicsCore.GetDXObject().GetDxCommand());
			target->Clear(*graphicsCore.GetDXObject().GetDxCommand(),
				Engine::MultiRenderTargetClearDesc{
					.clearColor = true,
					.clearColorValue = Engine::Color4::Black(),
				});
		}
	}

	bool CanCopyColor(const Engine::MultiRenderTarget* source,
		const Engine::MultiRenderTarget* destination) {

		if (!source || !destination || !source->GetColorTexture(0) ||
			!destination->GetColorTexture(0)) {
			return false;
		}
		return source->GetWidth() == destination->GetWidth() &&
			source->GetHeight() == destination->GetHeight() &&
			source->GetColorTexture(0)->GetFormat() ==
			destination->GetColorTexture(0)->GetFormat();
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

	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
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
	const RenderFeatureHierarchyItem* skippedSelection = nullptr;
	MultiRenderTarget* selectionTarget = nullptr;
	std::string selectionAlias{};
	std::vector<const RenderItem*> selectionItems{};
	std::vector<const RenderItem*> phaseItems{};
	for (size_t nodeIndex = 0; nodeIndex < plan.nodes.size(); ++nodeIndex) {

		const RenderFeaturePlanNode& node = plan.nodes[nodeIndex];

		if (!node.pass) {
			continue;
		}
		const RenderFeatureHierarchyItem* selectionGroup =
			node.selectionGroup;
		const bool selectionBegin = node.selectionBegin;
		const bool selectionEnd = node.selectionEnd;
		if (selectionBegin) {

			const RenderFeatureSelectionSettings& selection =
				selectionGroup->selection;
			const bool groupEnabled = service.GetRuntime().IsGroupEnabled(
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
				selectionTarget = deps_.postProcessTargetPool->Acquire(
					graphicsCore, *context.targetRegistry, desc, *sceneFinal);
				if (!selectionTarget ||
					!selectionTarget->GetColorTexture(0)) {

					Logger::Output(LogType::Engine, spdlog::level::err,
						"[レンダー機能] 選択描画先を作成できません グループ={}",
						selectionGroup->name);
					return;
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
						context, phaseItems, deps_, RenderPassSurfaceBinding{
							.colorSurface = selectionTarget,
							.depthOverride = depth,
						}, passKind);
				}
				selectionTarget->TransitionForShaderRead(
					*graphicsCore.GetDXObject().GetDxCommand());
			}
		}
		if (skippedSelection && skippedSelection == selectionGroup) {
			if (selectionEnd) {
				skippedSelection = nullptr;
				selectionTarget = nullptr;
				selectionAlias.clear();
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

			(void)shaderResource;
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
			const std::string stateKey = MakeStateKey(context.kind, pass.id);
			AdaptiveResolutionState& state =
				adaptiveResolutionStates_[stateKey];
			const float minScale = std::clamp(
				pass.minResolutionScale, 0.25f, 1.0f);
			const float maxScale = std::clamp(
				pass.maxResolutionScale, minScale, 1.0f);
			state.scale = std::clamp(state.scale, minScale, maxScale);
			const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
			const uint64_t interval = (std::max)(
				1u, pass.adjustmentIntervalFrames);
			if (frameSerial >= state.lastAdjustmentFrame + interval) {

				const float gpuMs = FrameProfiler::GetInstance().
					FindGPUPassMs(profileName);
				const float step = std::clamp(
					pass.resolutionStep, 0.05f, 0.5f);
				if (gpuMs > 0.0f) {

					state.filteredGpuMs = state.filteredGpuMs <= 0.0f ?
						gpuMs : state.filteredGpuMs +
						(gpuMs - state.filteredGpuMs) * 0.25f;
					if (state.filteredGpuMs > pass.gpuBudgetMs * 1.10f) {

						++state.overBudgetSamples;
						state.underBudgetSamples = 0;
					} else if (state.filteredGpuMs < pass.gpuBudgetMs * 0.55f) {

						++state.underBudgetSamples;
						state.overBudgetSamples = 0;
					} else {

						state.overBudgetSamples = 0;
						state.underBudgetSamples = 0;
					}

					// 解像度変更は履歴を破棄するため、負荷が継続した場合のみ変更する
					if (2 <= state.overBudgetSamples) {

						state.scale = (std::max)(
							minScale, state.scale - step);
						state.overBudgetSamples = 0;
						state.underBudgetSamples = 0;
					} else if (6 <= state.underBudgetSamples) {

						state.scale = (std::min)(
							maxScale, state.scale + step);
						state.overBudgetSamples = 0;
						state.underBudgetSamples = 0;
					}
				}
				state.lastAdjustmentFrame = frameSerial;
			}
			adaptiveScale = state.scale;
		}
		// 依存チェーン全体で同じ動的解像度係数を使い、各出力の基準倍率へ重ねて適用する
		const float outputScale = pass.adaptiveResolution ?
			adaptiveScale : dependencyScale;
		resolutionScales[pass.id.value] = outputScale;
		const RenderFeatureOutputSettings primaryOutput =
			GetPrimaryOutput(pass);
		const RenderFeatureOutputReference primaryReference{
			.pass = pass.id,
			.output = primaryOutput.name,
		};

		std::unordered_map<std::string, MultiRenderTarget*> outputTargets{};
		std::unordered_map<std::string, std::string> historyInputs{};
		std::vector<std::string> writtenHistoryKeys{};
		const std::vector<RenderFeatureOutputSettings> defaultOutputs =
			pass.outputs.empty() ?
			std::vector<RenderFeatureOutputSettings>{ primaryOutput } :
			pass.outputs;
		for (const RenderFeatureOutputSettings& output : defaultOutputs) {

			const RenderFeatureOutputReference reference{
				.pass = pass.id,
				.output = output.name,
			};
			PostProcessTemporaryTargetDesc desc{};
			const std::string outputAlias = MakeOutputAlias(reference);
			const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
			desc.name = output.history ? outputAlias + "_History" +
				std::to_string(frameSerial & 1u) : outputAlias;
			desc.format = ToDXGIFormat(output.format, sceneFormat);
			const bool forceFullResolution = raytracingChain &&
				!runtimeFeatures.useRaytracingDownsampling;
			desc.widthScale = forceFullResolution ? 1.0f :
				output.widthScale * outputScale;
			desc.heightScale = forceFullResolution ? 1.0f :
				output.heightScale * outputScale;
			MultiRenderTarget* target = deps_.postProcessTargetPool->Acquire(
				graphicsCore, *context.targetRegistry, desc, *sceneFinal);
			if (!target || !target->GetColorTexture(0)) {
				Logger::Output(LogType::Engine, spdlog::level::err,
					"[レンダー機能] 出力の作成に失敗しました パス={} 出力={}",
					pass.name, output.name);
				return;
			}
			if (output.history) {

				PostProcessTemporaryTargetDesc previousDesc = desc;
				previousDesc.name = outputAlias + "_History" +
					std::to_string((frameSerial + 1u) & 1u);
				MultiRenderTarget* previous = deps_.postProcessTargetPool->Acquire(
					graphicsCore, *context.targetRegistry, previousDesc, *sceneFinal);
				if (!previous || !previous->GetColorTexture(0)) {
					return;
				}
				const std::string historyKey = MakeStateKey(
					context.kind, pass.id, output.name);
				HistoryState& state = historyStates_[historyKey];
				const bool resetHistory = state.width != target->GetWidth() ||
					state.height != target->GetHeight() ||
					state.runtimeGeneration != service.GetRuntimeGeneration() ||
					state.raytracingMaterialGeneration !=
						context.raytracing.materialGeneration;
				if (resetHistory) {
					previous->TransitionForRender(
						*graphicsCore.GetDXObject().GetDxCommand());
					previous->Clear(
						*graphicsCore.GetDXObject().GetDxCommand(),
						MultiRenderTargetClearDesc{
							.clearColor = true,
							.clearColorValue = Color4::Black(),
						});
					state.width = target->GetWidth();
					state.height = target->GetHeight();
					state.runtimeGeneration = service.GetRuntimeGeneration();
					state.raytracingMaterialGeneration =
						context.raytracing.materialGeneration;
					state.valid = false;
				}
				context.targetRegistry->Register(outputAlias, target,
					{ outputAlias }, std::nullopt);
				if (!output.historyShaderResource.empty()) {
					historyInputs[output.historyShaderResource] = previousDesc.name;
				}
				writtenHistoryKeys.emplace_back(historyKey);
			}
			if (output.clearColor.has_value()) {
				target->TransitionForRender(
					*graphicsCore.GetDXObject().GetDxCommand());
				target->Clear(*graphicsCore.GetDXObject().GetDxCommand(),
					MultiRenderTargetClearDesc{
						.clearColor = true,
						.clearColorValue = output.clearColor,
					});
			}
			outputTargets[output.shaderResource] = target;
		}

		MultiRenderTarget* primaryTarget =
			ResolveOutputTarget(*context.targetRegistry, primaryReference);
		const bool isolatedSource = selectionBegin && selectionGroup &&
			selectionGroup->selection.mode ==
				RenderFeatureSelectionMode::IsolatedLayer;
		MultiRenderTarget* sourceTarget = isolatedSource ? selectionTarget :
			(node.source.pass ? ResolveOutputTarget(
				*context.targetRegistry, node.source) : sceneFinal);
		if (!primaryTarget || !sourceTarget) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レンダー機能] グラフリソースを解決できません パス={}", pass.name);
			return;
		}
		const std::string sourceAlias = isolatedSource ? selectionAlias :
			(node.source.pass ? MakeOutputAlias(node.source) :
				RenderTargetNames::kSceneColorFinal);
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
		std::string executionPrimaryAlias = MakeOutputAlias(primaryReference);
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
			outputTargets[primaryOutput.shaderResource] =
				executionPrimaryTarget;
		}
		const bool bypassPass =
			(runtimeOverride && runtimeOverride->enabled == false) ||
			rayTracingUnavailable || rayTracingMaterialsPending;
		if (bypassPass) {

			ClearOutputTargets(graphicsCore, outputTargets);
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
			for (const auto& [resourceName, targetName] : historyInputs) {
				inputs.insert_or_assign(resourceName, targetName);
			}
			inputs[PostProcessBindingNames::kSourceColor] = sourceAlias;
			for (const auto& [name, reference] : pass.passInputs) {

				inputs[name] = MakeOutputAlias(reference);
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
					for (const MaterialParameterRecord& parameter :
						runtimeOverride->parameters.GetRecords()) {

						desc.parameterOverrides.Set(parameter.id,
							parameter.namedValue.first, parameter.semantic,
							parameter.namedValue.second);
					}
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
				for (const auto& [resourceName, target] : outputTargets) {

					if (target == executionPrimaryTarget) {
						desc.outputTargets[resourceName] = executionPrimaryAlias;
						continue;
					}
					const auto output = std::find_if(defaultOutputs.begin(),
						defaultOutputs.end(),
						[&](const RenderFeatureOutputSettings& settings) {

							return settings.shaderResource == resourceName;
						});
					if (output == defaultOutputs.end()) {
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
				for (const auto& [resourceName, target] : outputTargets) {

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
		for (const std::string& historyKey : writtenHistoryKeys) {
			historyStates_[historyKey].valid = true;
		}

		if (useSelectionComposite) {

			PostProcessExecutionDesc composite{};
			composite.material =
				BuiltinAssets::Materials::PostProcessMaskComposite;
			composite.passKind = MaterialPassKind::PostProcess;
			composite.source.colors = {
				RenderTargetNames::kSceneColorFinal };
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
			if (!deps_.postProcessExecutor->Execute(graphicsCore,
				RenderFrameRequest{}, context, *deps_.assetLibrary,
				*deps_.pipelineCache, composite)) {

				Logger::Output(LogType::Engine, spdlog::level::err,
					"[レンダー機能] 選択描画の合成に失敗しました パス={}",
					pass.name);
				return;
			}
			if (!MultiRenderTargetCopy::CopyColor0Resource(
				graphicsCore, primaryTarget, sceneFinal)) {

				Logger::Output(LogType::Engine, spdlog::level::err,
					"[レンダー機能] 選択描画をScene Colorへ反映できません グループ={}",
					selectionGroup->name);
				return;
			}
			selectionTarget = nullptr;
			selectionAlias.clear();
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
