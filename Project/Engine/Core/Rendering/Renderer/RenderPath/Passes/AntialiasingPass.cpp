#include "AntialiasingPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTargetCopyUtility.h>

// c++
#include <utility>

namespace {

	constexpr const char* kAntialiasingInput = "AntialiasingInput";

	bool IsEnabled(const Engine::GraphicsFeaturePreferences& preferences) {

		return preferences.antialiasing == Engine::AntialiasingMethod::FXAA &&
			preferences.fxaa.strength > 0.0f && preferences.fxaa.subpixelStrength > 0.0f;
	}

	bool IsPartialView(const Engine::SceneExecutionContext& context, const Engine::MultiRenderTarget& surface) {

		return context.useViewportRect && (context.viewportX != 0 || context.viewportY != 0 ||
			context.viewportWidth != surface.GetWidth() || context.viewportHeight != surface.GetHeight());
	}
}

//============================================================================
//	AntialiasingPass classMethods
//============================================================================
bool Engine::AntialiasingPass::CanRenderToView(const GraphicsFeaturePreferences& preferences, const SceneExecutionContext& context) {

	if (!IsEnabled(preferences) || !context.defaultSurface || !context.targetRegistry ||
		context.targetRegistry->Find("View") != context.defaultSurface) return false;
	const RenderTexture2D* color = context.defaultSurface->GetColorTexture(0);
	if (!color || !color->GetResource() || !color->GetUAVGPUHandle().ptr) return false;
	// sRGB変換や整数出力を必要とする独自Viewは従来の転送を使う
	return color->GetFormat() == DXGI_FORMAT_R32G32B32A32_FLOAT || color->GetFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT;
}

Engine::MultiRenderTarget* Engine::AntialiasingPass::PrepareToneMapTarget(GraphicsCore& graphicsCore,
	SceneExecutionContext& context, const RenderPipelineDeps& deps) {

	const auto& preferences = graphicsCore.GetDXObject().GetFeatureController().GetPreferences();
	if (!CanRenderToView(preferences, context) || !deps.postProcessTargetPool || !deps.postProcessExecutor ||
		!deps.assetLibrary || !deps.pipelineCache) return nullptr;
	PostProcessTemporaryTargetDesc desc{};
	desc.name = kAntialiasingInput;
	desc.format = context.defaultSurface->GetColorTexture(0)->GetFormat();
	desc.createUAV = false;
	auto* target = deps.postProcessTargetPool->Acquire(graphicsCore, *context.targetRegistry, desc, *context.defaultSurface);
	if (!target || target == context.defaultSurface) return nullptr;
	// 失敗時の復帰にも使うため分割Cameraの範囲外を維持する
	if (IsPartialView(context, *context.defaultSurface) &&
		!MultiRenderTargetCopy::CopyColor0Resource(graphicsCore, context.defaultSurface, target)) return nullptr;
	return target;
}

void Engine::AntialiasingPass::Execute(GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	MultiRenderTarget* preparedSource = std::exchange(context.antialiasingSource, nullptr);
	const auto restoreToneMap = [&] {
		if (preparedSource) MultiRenderTargetCopy::CopyColor0Resource(graphicsCore, preparedSource, context.defaultSurface);
	};
	const auto& preferences = graphicsCore.GetDXObject().GetFeatureController().GetPreferences();
	if (!IsEnabled(preferences) || !context.defaultSurface || !context.targetRegistry ||
		!deps_.postProcessTargetPool || !deps_.postProcessExecutor || !deps_.assetLibrary || !deps_.pipelineCache) {
		restoreToneMap();
		return;
	}
	const bool directOutput = preparedSource && CanRenderToView(preferences, context);
	if (preparedSource && !directOutput) restoreToneMap();
	auto* source = directOutput ? preparedSource : context.defaultSurface;
	if (!source->GetColorTexture(0)) return;
	MultiRenderTarget* target = context.defaultSurface;
	if (!directOutput) {
		PostProcessTemporaryTargetDesc targetDesc{};
		targetDesc.name = "AntialiasingOutput";
		targetDesc.format = source->GetColorTexture(0)->GetFormat();
		target = deps_.postProcessTargetPool->Acquire(graphicsCore, *context.targetRegistry, targetDesc, *source);
		if (!target) return;
		// 全面書込みでは初期コピー不要、分割Cameraの範囲外だけ保持する
		if (IsPartialView(context, *source) && !MultiRenderTargetCopy::CopyColor0Resource(graphicsCore, source, target)) return;
	}
	PostProcessExecutionDesc desc{};
	desc.material = BuiltinAssets::Materials::FXAA;
	desc.source.colors = {directOutput ? kAntialiasingInput : "View"};
	desc.dest.colors = {directOutput ? "View" : "AntialiasingOutput"};
	desc.parameterOverrides.Set("strength", MaterialParameterValue{.value = preferences.fxaa.strength});
	desc.parameterOverrides.Set("edgeThreshold", MaterialParameterValue{.value = preferences.fxaa.edgeThreshold});
	desc.parameterOverrides.Set("minimumThreshold", MaterialParameterValue{.value = preferences.fxaa.minimumThreshold});
	desc.parameterOverrides.Set("subpixelStrength", MaterialParameterValue{.value = preferences.fxaa.subpixelStrength});
	desc.parameterOverrides.Set("viewRect", MaterialParameterValue{.value = Vector4(
		static_cast<float>(context.useViewportRect ? context.viewportX : 0),
		static_cast<float>(context.useViewportRect ? context.viewportY : 0),
		static_cast<float>(context.useViewportRect ? context.viewportWidth : source->GetWidth()),
		static_cast<float>(context.useViewportRect ? context.viewportHeight : source->GetHeight()))});
	if (deps_.postProcessExecutor->Execute(graphicsCore, {}, context, *deps_.assetLibrary, *deps_.pipelineCache, desc)) {
		if (!directOutput) MultiRenderTargetCopy::CopyColor0Resource(graphicsCore, target, context.defaultSurface);
	} else {
		restoreToneMap();
	}
}
