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

	// Transparentバケットを半透明設定でSceneFinalへ合成する、Opaque結果の上に重ねる
	RenderPassExecutionHelper::Execute(graphicsCore, context, passBuckets, deps_,
		RenderPhase::Transparent, context.resources->GetSceneFinal(), MaterialPassKind::Transparent);
}
