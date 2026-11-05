#include "AntialiasingPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTargetCopyUtility.h>

//============================================================================
//	AntialiasingPass classMethods
//============================================================================
void Engine::AntialiasingPass::Execute(GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	const auto& preferences = graphicsCore.GetDXObject().GetFeatureController().GetPreferences();
	if (preferences.antialiasing != AntialiasingMethod::FXAA) return;
	if (!context.defaultSurface || !context.targetRegistry) return;
	auto* source = context.defaultSurface;
	PostProcessTemporaryTargetDesc targetDesc{};
	targetDesc.name = "AntialiasingOutput";
	targetDesc.format = source->GetColorTexture(0)->GetFormat();
	auto* target = deps_.postProcessTargetPool->Acquire(graphicsCore, *context.targetRegistry, targetDesc, *source);
	if (!target || !MultiRenderTargetCopy::CopyColor0Resource(graphicsCore, source, target)) return;
	PostProcessExecutionDesc desc{};
	desc.material = BuiltinAssets::Materials::FXAA;
	desc.source.colors = { "View" };
	desc.dest.colors = { targetDesc.name };
	desc.parameterOverrides.Set("strength", MaterialParameterValue{.value = preferences.fxaa.strength});
	desc.parameterOverrides.Set("edgeThreshold", MaterialParameterValue{.value = preferences.fxaa.edgeThreshold});
	desc.parameterOverrides.Set("minimumThreshold", MaterialParameterValue{.value = preferences.fxaa.minimumThreshold});
	desc.parameterOverrides.Set("subpixelStrength", MaterialParameterValue{.value = preferences.fxaa.subpixelStrength});
	desc.parameterOverrides.Set("viewRect", MaterialParameterValue{.value = Vector4(
		static_cast<float>(context.useViewportRect ? context.viewportX : 0),
		static_cast<float>(context.useViewportRect ? context.viewportY : 0),
		static_cast<float>(context.useViewportRect ? context.viewportWidth : source->GetWidth()),
		static_cast<float>(context.useViewportRect ? context.viewportHeight : source->GetHeight()))});
	if (deps_.postProcessExecutor->Execute(graphicsCore, {}, context, *deps_.assetLibrary, *deps_.pipelineCache, desc))
		MultiRenderTargetCopy::CopyColor0Resource(graphicsCore, target, source);
}
