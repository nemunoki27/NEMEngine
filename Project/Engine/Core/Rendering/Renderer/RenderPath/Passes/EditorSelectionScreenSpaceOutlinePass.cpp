#include "EditorSelectionScreenSpaceOutlinePass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Outline/EditorSelectionOutlineRequestService.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>

// c++
#include <cmath>

//============================================================================
//	EditorSelectionScreenSpaceOutlinePass classMethods
//============================================================================
void Engine::EditorSelectionScreenSpaceOutlinePass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// 選択アウトラインはSceneViewのEditor描画専用
	if (!context.resources || context.kind != RenderViewKind::Scene) {
		return;
	}

	// Editorの選択サービスが積んだ要求から、現worldかつwidthが有効なものだけ拾う
	requests_.clear();
	for (const ScreenSpaceOutlineRequest& request :
		EditorSelectionOutlineRequestService::GetInstance().GetRequests()) {

		if (!request.world || request.world != context.world) {
			continue;
		}
		if (!std::isfinite(request.style.widthPixels) || request.style.widthPixels <= 0.0f) {
			continue;
		}
		requests_.emplace_back(request);
	}
	if (requests_.empty()) {
		return;
	}

	// Editor専用のScreenSpaceOutlineへ描いてruntime用とは分離する
	renderer_.Render(graphicsCore, context, passBuckets, deps_, requests_,
		context.resources->GetEditorSelectionScreenSpaceOutline());
#else
	(void)graphicsCore;
	(void)passBuckets;
	(void)context;
#endif
}
