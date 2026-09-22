#include "RenderFeaturePassTargets.h"

//============================================================================
//	include
//============================================================================
#include "RenderFeatureResourceUtility.h"
#include "RenderFeatureTemporalState.h"
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

bool Engine::RenderFeaturePassTargets::Resolve(GraphicsCore& graphicsCore, SceneExecutionContext& context, PostProcessTemporaryTargetPool& pool,
			RenderFeatureTemporalState& temporalState, const RenderFeaturePassSettings& pass, MultiRenderTarget& sceneFinal,
			DXGI_FORMAT sceneFormat, float outputScale, bool raytracingChain, const GraphicsRuntimeFeatures& runtimeFeatures,
			uint64_t runtimeGeneration) {

	primaryOutput = GetPrimaryOutput(pass);
	primaryReference = RenderFeatureOutputReference{
		.pass = pass.id,
		.output = primaryOutput.name,
	};

	defaultOutputs =
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
		MultiRenderTarget* target = pool.Acquire(
			graphicsCore, *context.targetRegistry, desc, sceneFinal);
		if (!target || !target->GetColorTexture(0)) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レンダー機能] 出力の作成に失敗しました パス={} 出力={}",
				pass.name, output.name);
			return false;
		}
		if (output.history) {

			PostProcessTemporaryTargetDesc previousDesc = desc;
			previousDesc.name = outputAlias + "_History" +
				std::to_string((frameSerial + 1u) & 1u);
			MultiRenderTarget* previous = pool.Acquire(
				graphicsCore, *context.targetRegistry, previousDesc, sceneFinal);
			if (!previous || !previous->GetColorTexture(0)) {
				return false;
			}
			const std::string historyKey = MakeStateKey(
				context.kind, pass.id, output.name);
			temporalState.PrepareHistory(graphicsCore, previous, historyKey, target->GetWidth(), target->GetHeight(),
				runtimeGeneration, context.raytracing.materialGeneration);
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

	return true;
}
