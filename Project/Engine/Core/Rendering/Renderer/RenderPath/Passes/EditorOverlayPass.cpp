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
		SceneComponentOverlayState::GetInstance().Clear();
		return;
	}

	// 表示すべきコンポーネントアイコンを集め、1件も無ければstateを片付ける
	collector_.Collect(*context.world, *context.view, registry_, settings_, items_);
	if (items_.empty()) {
		SceneComponentOverlayState::GetInstance().Clear();
		return;
	}
	// SceneViewの前面へアイコンを描画
	renderer_.Render(graphicsCore, *context.assetDatabase, *context.view, *context.defaultSurface, context.world, items_);
#endif
}
