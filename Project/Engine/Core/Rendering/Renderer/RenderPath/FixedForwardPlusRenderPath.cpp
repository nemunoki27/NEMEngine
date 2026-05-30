#include "FixedForwardPlusRenderPath.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/ClearRenderTargetsPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/DepthPrepass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/LightCullingPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/OpaqueRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/RaytracingReflectionPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/InvertedHullOutlinePass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/TransparentRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/PostProcessMaskedUiPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/PostProcessStackPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/BlitToViewPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/ScreenUiPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/DebugOverlayPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/EditorOverlayPass.h>

//============================================================================
//	FixedForwardPlusRenderPath classMethods
//============================================================================

void Engine::FixedForwardPlusRenderPath::Initialize(const RenderPipelineDeps& deps) {

	deps_ = deps;
	passes_.clear();
	passes_.reserve(13);

	passes_.emplace_back(std::make_unique<ClearRenderTargetsPass>(deps_));
	passes_.emplace_back(std::make_unique<DepthPrepass>(deps_));
	passes_.emplace_back(std::make_unique<LightCullingPass>(deps_));
	passes_.emplace_back(std::make_unique<OpaqueRenderPass>(deps_));
	passes_.emplace_back(std::make_unique<RaytracingReflectionPass>(deps_));
	passes_.emplace_back(std::make_unique<InvertedHullOutlinePass>(deps_));
	passes_.emplace_back(std::make_unique<TransparentRenderPass>(deps_));
	passes_.emplace_back(std::make_unique<PostProcessMaskedUiPass>(deps_));
	passes_.emplace_back(std::make_unique<PostProcessStackPass>(deps_));
	passes_.emplace_back(std::make_unique<BlitToViewPass>(deps_));
	passes_.emplace_back(std::make_unique<ScreenUiPass>(deps_));
	passes_.emplace_back(std::make_unique<DebugOverlayPass>());
	passes_.emplace_back(std::make_unique<EditorOverlayPass>());
}

void Engine::FixedForwardPlusRenderPath::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	for (auto& pass : passes_) {
		pass->Execute(graphicsCore, passBuckets, context);
	}
}
