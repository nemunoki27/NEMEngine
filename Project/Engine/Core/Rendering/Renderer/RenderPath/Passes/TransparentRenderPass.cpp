#include "TransparentRenderPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>

//============================================================================
//	TransparentRenderPass classMethods
//============================================================================

void Engine::TransparentRenderPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	// 共有リソースが無ければ描画先が決まらないので抜ける
	if (!context.resources) {
		return;
	}

	// シーンテクスチャ取得
	MultiRenderTarget* sceneMain = context.resources->GetSceneMain();
	DepthTexture2D* sceneDepth = sceneMain->GetDepthTexture();

	// 半透明パスの描画
	RenderPassExecutionHelper::Execute(graphicsCore, context, passBuckets, deps_, RenderPhase::Transparent,
		context.resources->GetSceneFinal(), MaterialPassKind::Transparent, false, sceneDepth);
}
