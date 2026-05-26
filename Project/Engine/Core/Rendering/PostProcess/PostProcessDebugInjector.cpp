#include "PostProcessDebugInjector.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetGenerator.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessExecutor.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessTemporaryTargetPool.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>

//============================================================================
//	PostProcessDebugInjector classMethods
//============================================================================

bool Engine::PostProcessDebugInjector::TryExecuteBeforeBlit(GraphicsCore& graphicsCore,
	const SceneExecutionContext& context,
	std::string_view sourceName, std::string_view destName,
	RenderAssetLibrary& assetLibrary, PipelineStateCache& pipelineCache,
	PostProcessExecutor& executor, PostProcessTemporaryTargetPool& targetPool,
	const PostProcessAssetGenerator& assetGenerator, MultiRenderTarget*& inoutSource) {

	if (!settings_.enabled || !context.targetRegistry || !inoutSource ||
		sourceName != settings_.sourceName || destName != "View") {
		return false;
	}

	PostProcessTemporaryTargetDesc tempDesc{};
	tempDesc.name = settings_.tempName;
	if (inoutSource->GetColorCount() != 0 && inoutSource->GetColorTexture(0)) {
		tempDesc.format = inoutSource->GetColorTexture(0)->GetFormat();
	}
	tempDesc.createUAV = true;

	MultiRenderTarget* temp = targetPool.Acquire(graphicsCore, *context.targetRegistry, tempDesc, *inoutSource);
	if (!temp) {
		return false;
	}

	AssetID material = assetGenerator.FindBuiltinMaterial(settings_.materialName);
	if (!material) {
		return false;
	}

	PostProcessExecutionDesc desc{};
	desc.material = material;
	desc.passName = "PostProcess";
	desc.source.colors = { std::string(sourceName) };
	desc.dest.colors = { settings_.tempName };
	desc.dispatchMode = ComputeDispatchMode::FromDestSize;

	if (!executor.Execute(graphicsCore, RenderFrameRequest{}, context, assetLibrary, pipelineCache, desc)) {
		return false;
	}

	// 以降のFullscreen Blitは、元のSceneColorFinalではなく中間RTを読む。
	inoutSource = temp;
	return true;
}
