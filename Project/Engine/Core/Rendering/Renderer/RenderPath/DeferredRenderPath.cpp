#include "DeferredRenderPath.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxGPUEventScope.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/ClearRenderTargetsPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/SkyboxPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/DepthPrepass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/OpaqueRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/LightingPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/RaytracingReflectionPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/InvertedHullOutlinePass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/TransparentRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/RuntimeScreenSpaceOutlinePass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/PostProcessMaskedUIPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/PostProcessStackPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/EditorSelectionScreenSpaceOutlinePass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/BlitToViewPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/ScreenUIPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/DebugOverlayPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/EditorOverlayPass.h>

//============================================================================
//	DeferredRenderPath classMethods
//============================================================================
void Engine::DeferredRenderPath::Initialize(const RenderPipelineDeps& deps) {

	deps_ = deps;
	passes_.reserve(15);

	// 設定されたパス順に実行される
	passes_.emplace_back(std::make_unique<ClearRenderTargetsPass>(deps_));
	passes_.emplace_back(std::make_unique<DepthPrepass>(deps_));
	passes_.emplace_back(std::make_unique<OpaqueRenderPass>(deps_));
	passes_.emplace_back(std::make_unique<LightingPass>());
	passes_.emplace_back(std::make_unique<RaytracingReflectionPass>(deps_));
	passes_.emplace_back(std::make_unique<InvertedHullOutlinePass>(deps_));
	passes_.emplace_back(std::make_unique<TransparentRenderPass>(deps_));
	passes_.emplace_back(std::make_unique<RuntimeScreenSpaceOutlinePass>(deps_));
	passes_.emplace_back(std::make_unique<PostProcessMaskedUIPass>(deps_));
	passes_.emplace_back(std::make_unique<PostProcessStackPass>(deps_));
	passes_.emplace_back(std::make_unique<EditorSelectionScreenSpaceOutlinePass>(deps_));
	passes_.emplace_back(std::make_unique<BlitToViewPass>(deps_));
	passes_.emplace_back(std::make_unique<ScreenUIPass>(deps_));
	passes_.emplace_back(std::make_unique<DebugOverlayPass>());
	passes_.emplace_back(std::make_unique<EditorOverlayPass>());
}

void Engine::DeferredRenderPath::Finalize() {

	for (auto& pass : passes_) {
		pass.reset();
	}
	passes_.clear();
	deps_ = {};
}

void Engine::DeferredRenderPath::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	for (auto& pass : passes_) {

		ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();

		// イベントパス名を取得、シーンごとに名前を分ける
		std::string eventPassName = std::string(EnumAdapter<RenderViewKind>::ToStringView(context.kind)) + "/" +
			std::string(EnumAdapter<RenderPathPassKind>::ToStringView(pass->GetKind()));

		// GPUPIXイベント発行
		DxGPUEventScope eventScope{ commandList, eventPassName };

		// GPU計測
		GPUFrameProfiler::GetInstance().BeginPass(commandList, eventPassName);

		// パス実行
		pass->Execute(graphicsCore, passBuckets, context);

		GPUFrameProfiler::GetInstance().EndPass(commandList);
	}
}
