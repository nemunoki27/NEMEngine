#include "EditorOverlayPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayState.h>

//============================================================================
//	EditorOverlayPass classMethods
//============================================================================

void Engine::EditorOverlayPass::Execute([[maybe_unused]] GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, [[maybe_unused]] SceneExecutionContext& context) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// SceneViewでOverlay許可がある時だけ描く、前提が崩れたらstateを片付けて抜ける
	if (context.kind != RenderViewKind::Scene ||
		!context.allowSceneComponentOverlay ||
		!context.world || !context.view || !context.view->valid ||
		!context.defaultSurface || !context.resources || !context.assetDatabase) {
		SceneComponentOverlayState::GetInstance().Clear(context.world);
		return;
	}

	// 表示すべきコンポーネントアイコンを集め、1件も無ければstateを片付ける
	collector_.Collect(*context.world, *context.view, registry_, settings_, items_);
	if (items_.empty()) {
		SceneComponentOverlayState::GetInstance().Clear(context.world);
		return;
	}
	// 深度を渡してSceneの遮蔽に合わせてアイコンを描く
	DepthTexture2D* sceneDepth = nullptr;
	if (MultiRenderTarget* sceneMain = context.resources->GetSceneMain()) {
		sceneDepth = sceneMain->GetDepthTexture();
	}
	renderer_.Render(graphicsCore, *context.assetDatabase, *context.view, *context.defaultSurface, sceneDepth, context.world, items_);
#endif
}
