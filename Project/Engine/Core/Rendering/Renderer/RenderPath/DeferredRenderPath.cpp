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
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/QueueRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/LightingPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/RenderFeaturePass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/InvertedHullOutlinePass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/RuntimeScreenSpaceOutlinePass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/EditorSelectionScreenSpaceOutlinePass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/BlitToViewPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/DebugOverlayPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/EditorOverlayPass.h>

// c++
#include <array>

//============================================================================
//	DeferredRenderPath classMethods
//============================================================================
void Engine::DeferredRenderPath::Initialize(const RenderPipelineDeps& deps) {

	deps_ = deps;

	// 固定のパス列、RenderFeatureはAnchor位置へ後から挿入する
	std::vector<std::unique_ptr<IRenderPass>> fixedPasses;
	fixedPasses.reserve(13);
	fixedPasses.emplace_back(std::make_unique<ClearRenderTargetsPass>(deps_));
	fixedPasses.emplace_back(std::make_unique<DepthPrepass>(deps_));
	fixedPasses.emplace_back(std::make_unique<QueueRenderPass>(deps_, QueueRenderPass::Desc{
		.kind = RenderPathPassKind::Opaque,
		.phase = RenderPhase::Opaque,
		.target = QueueRenderPass::Target::SceneMain
		}));
	fixedPasses.emplace_back(std::make_unique<LightingPass>());
	fixedPasses.emplace_back(std::make_unique<InvertedHullOutlinePass>(deps_));
	fixedPasses.emplace_back(std::make_unique<QueueRenderPass>(deps_, QueueRenderPass::Desc{
		.kind = RenderPathPassKind::Transparent,
		.phase = RenderPhase::Transparent,
		.target = QueueRenderPass::Target::SceneFinal,
		.materialPass = MaterialPassKind::Transparent,
		.reuseSceneDepth = true
		}));
	fixedPasses.emplace_back(std::make_unique<RuntimeScreenSpaceOutlinePass>(deps_));
	fixedPasses.emplace_back(std::make_unique<QueueRenderPass>(deps_, QueueRenderPass::Desc{
		.kind = RenderPathPassKind::PostProcessMaskedUI,
		.phase = RenderPhase::PostProcessMaskedUI,
		.target = QueueRenderPass::Target::SceneFinal,
		.usePhaseExecution = false
		}));
	fixedPasses.emplace_back(std::make_unique<EditorSelectionScreenSpaceOutlinePass>(deps_));
	fixedPasses.emplace_back(std::make_unique<BlitToViewPass>(deps_));
	fixedPasses.emplace_back(std::make_unique<QueueRenderPass>(deps_, QueueRenderPass::Desc{
		.kind = RenderPathPassKind::ScreenUI,
		.phase = RenderPhase::ScreenUI,
		.target = QueueRenderPass::Target::DefaultSurface,
		.usePhaseExecution = false
		}));
	fixedPasses.emplace_back(std::make_unique<DebugOverlayPass>());
	fixedPasses.emplace_back(std::make_unique<EditorOverlayPass>());

	// アンカーと、その直前に置く固定パス種別の対応表
	struct AnchorPoint {
		RenderFeatureAnchor anchor;
		RenderPathPassKind after;
	};
	const std::array<AnchorPoint, 6> kAnchorPoints = { {
		{ RenderFeatureAnchor::BeforeLighting, RenderPathPassKind::Opaque },
		{ RenderFeatureAnchor::AfterLighting, RenderPathPassKind::Lighting },
		{ RenderFeatureAnchor::BeforeTransparent, RenderPathPassKind::InvertedHullOutline },
		{ RenderFeatureAnchor::AfterTransparent, RenderPathPassKind::Transparent },
		{ RenderFeatureAnchor::AfterMaskedUI, RenderPathPassKind::PostProcessMaskedUI },
		{ RenderFeatureAnchor::BeforeBlit, RenderPathPassKind::EditorSelectionScreenSpaceOutline },
	} };

	// 固定パスを順に積みつつ、対応する位置へ共通Feature実行パスを挿入する
	passes_.reserve(fixedPasses.size() + kAnchorPoints.size());
	for (auto& pass : fixedPasses) {

		const RenderPathPassKind kind = pass->GetKind();
		passes_.emplace_back(std::move(pass));
		for (const AnchorPoint& point : kAnchorPoints) {
			if (point.after == kind) {
				passes_.emplace_back(
					std::make_unique<RenderFeaturePass>(deps_, point.anchor));
			}
		}
	}
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

		// RenderFeatureは内部ノード単位で計測する
		const bool profileWholePass =
			pass->GetKind() != RenderPathPassKind::RenderFeature;
		if (profileWholePass) {
			GPUFrameProfiler::GetInstance().BeginPass(
				commandList, eventPassName);
		}

		// パス実行
		pass->Execute(graphicsCore, passBuckets, context);

		if (profileWholePass) {
			GPUFrameProfiler::GetInstance().EndPass(commandList);
		}
	}
}
