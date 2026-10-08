#include "ColorPipelineProcessor.h"


//============================================================================
//	ColorPipelineProcessor stateMethods
//============================================================================
void Engine::ColorPipelineProcessor::RetainViews(const std::unordered_set<std::string>& activeViews) {

	// 描画されないCameraのBufferもGPU完了まで保持する
	for (auto it = viewStates_.begin(); it != viewStates_.end();) {
		if (!activeViews.contains(it->first)) {
			it->second.exposureBuffer.Release();
			it = viewStates_.erase(it);
		} else {
			++it;
		}
	}
}
