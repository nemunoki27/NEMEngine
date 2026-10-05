#include "RenderPipelineRunner.h"

//============================================================================
//	RenderPipelineRunner CameraMethods
//============================================================================
Engine::RenderPipelineViewResources& Engine::RenderPipelineRunner::GetCameraState(const ResolvedRenderView& view) {

	std::unique_ptr<RenderPipelineViewResources>& state = cameraStates_[view.GetHistoryKey()];
	if (!state) {
		// 初回だけCamera用の所有元を作る
		state = std::make_unique<RenderPipelineViewResources>();
	}
	state->view = view;
	return *state;
}

const Engine::RenderPipelineViewResources& Engine::RenderPipelineRunner::FindCameraState(RenderViewKind kind) const {

	const RenderPipelineViewResources& primary = kind == RenderViewKind::Game ? gameViewState_ : sceneViewState_;
	const auto found = cameraStates_.find(primary.view.GetHistoryKey());
	return found == cameraStates_.end() ? primary : *found->second;
}
