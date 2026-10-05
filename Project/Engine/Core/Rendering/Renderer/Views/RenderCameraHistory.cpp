#include "RenderCameraHistory.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

//============================================================================
//	RenderCameraHistory classMethods
//============================================================================
Engine::Matrix4x4 Engine::RenderCameraHistory::Update(
	const ResolvedRenderView& view, RenderCameraDomain domain, uint64_t frameSerial) {

	const ResolvedCameraView* camera = view.FindCamera(domain);
	if (!camera) {
		return Matrix4x4::Identity();
	}
	if (cleanupFrameSerial_ != frameSerial) {
		// 描画されなくなったCameraの履歴を回収する
		std::erase_if(states_, [frameSerial](const auto& entry) {
			return entry.second.frameSerial + 1 < frameSerial;
		});
		cleanupFrameSerial_ = frameSerial;
	}
	const std::string key = view.GetHistoryKey() + "|" + std::to_string(static_cast<uint32_t>(domain)) +
		"|" + std::to_string(camera->sourceCamera.index) + "|" + std::to_string(camera->sourceCamera.generation);
	State& state = states_[key];
	const bool reset = state.width != view.width || state.height != view.height ||
		state.projectionMode != camera->projectionMode || state.frameSerial + 1 < frameSerial;
	if (reset || state.frameSerial != frameSerial) {
		// 初回と条件変更時は現在の行列から履歴を始める
		state.previous = reset ? camera->matrices.viewProjectionMatrix : state.current;
		state.current = camera->matrices.viewProjectionMatrix;
		state.frameSerial = frameSerial;
		state.width = view.width;
		state.height = view.height;
		state.projectionMode = camera->projectionMode;
	}
	return state.previous;
}
