#include "OpaqueRenderPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>

//============================================================================
//	OpaqueRenderPass classMethods
//============================================================================
void Engine::OpaqueRenderPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	// 共有リソースが無ければ描画先が決まらないので抜ける
	if (!context.resources) {
		return;
	}

	// Opaqueバケットを不透明設定でSceneMainへ描く、深度はDepthPrepassの結果を流用する
	RenderPassExecutionHelper::Execute(graphicsCore, context, passBuckets, deps_,
		RenderPhase::Opaque, context.resources->GetSceneMain());
}