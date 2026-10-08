#include "RenderPipelineRunner.h"

//============================================================================
//	include
//============================================================================
// c++
#include <string>

namespace {

	// 所有元の読み書きに合わせてCamera資源を検索する
	template<typename T, typename Map>
	T& FindCameraResources(T& primary, Map& states, const std::string& historyKey) {

		auto found = states.find(historyKey);
		return found == states.end() ? primary : *found->second;
	}
}

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

Engine::RenderPipelineViewResources& Engine::RenderPipelineRunner::FindCameraState(RenderViewKind kind) {

	RenderPipelineViewResources& primary = kind == RenderViewKind::Game ? gameViewState_ : sceneViewState_;
	return FindCameraResources(primary, cameraStates_, primary.view.GetHistoryKey());
}

const Engine::RenderPipelineViewResources& Engine::RenderPipelineRunner::FindCameraState(RenderViewKind kind) const {

	const RenderPipelineViewResources& primary = kind == RenderViewKind::Game ? gameViewState_ : sceneViewState_;
	return FindCameraResources(primary, cameraStates_, primary.view.GetHistoryKey());
}
