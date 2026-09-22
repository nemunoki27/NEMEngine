#include "RenderFeatureTemporalState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <algorithm>

float Engine::RenderFeatureTemporalState::UpdateResolution(const RenderFeaturePassSettings& pass,
	const std::string& stateKey, const std::string& profileName) {

	AdaptiveResolutionState& state =
		adaptiveResolutionStates_[stateKey];
	const float minScale = std::clamp(
		pass.minResolutionScale, 0.25f, 1.0f);
	const float maxScale = std::clamp(
		pass.maxResolutionScale, minScale, 1.0f);
	state.scale = std::clamp(state.scale, minScale, maxScale);
	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	const uint64_t interval = (std::max)(
		1u, pass.adjustmentIntervalFrames);
	if (frameSerial >= state.lastAdjustmentFrame + interval) {

		const float gpuMs = FrameProfiler::GetInstance().
			FindGPUPassMs(profileName);
		const float step = std::clamp(
			pass.resolutionStep, 0.05f, 0.5f);
		if (gpuMs > 0.0f) {

			state.filteredGPUMs = state.filteredGPUMs <= 0.0f ?
				gpuMs : state.filteredGPUMs +
				(gpuMs - state.filteredGPUMs) * 0.25f;
			if (state.filteredGPUMs > pass.gpuBudgetMs * 1.10f) {

				++state.overBudgetSamples;
				state.underBudgetSamples = 0;
			} else if (state.filteredGPUMs < pass.gpuBudgetMs * 0.55f) {

				++state.underBudgetSamples;
				state.overBudgetSamples = 0;
			} else {

				state.overBudgetSamples = 0;
				state.underBudgetSamples = 0;
			}

			// 解像度変更は履歴を破棄するため、負荷が継続した場合のみ変更する
			if (2 <= state.overBudgetSamples) {

				state.scale = (std::max)(
					minScale, state.scale - step);
				state.overBudgetSamples = 0;
				state.underBudgetSamples = 0;
			} else if (6 <= state.underBudgetSamples) {

				state.scale = (std::min)(
					maxScale, state.scale + step);
				state.overBudgetSamples = 0;
				state.underBudgetSamples = 0;
			}
		}
		state.lastAdjustmentFrame = frameSerial;
	}
	return state.scale;
}

void Engine::RenderFeatureTemporalState::PrepareHistory(GraphicsCore& graphicsCore,
	MultiRenderTarget* previous, const std::string& historyKey, uint32_t width, uint32_t height,
	uint64_t runtimeGeneration, uint64_t materialGeneration) {

	HistoryState& state = historyStates_[historyKey];
	const bool resetHistory = state.width != width ||
		state.height != height ||
		state.runtimeGeneration != runtimeGeneration ||
		state.raytracingMaterialGeneration !=
			materialGeneration;
	if (resetHistory) {
		previous->TransitionForRender(
			*graphicsCore.GetDXObject().GetDxCommand());
		previous->Clear(
			*graphicsCore.GetDXObject().GetDxCommand(),
			MultiRenderTargetClearDesc{
				.clearColor = true,
				.clearColorValue = Color4::Black(),
			});
		state.width = width;
		state.height = height;
		state.runtimeGeneration = runtimeGeneration;
		state.raytracingMaterialGeneration =
			materialGeneration;
		state.valid = false;
	}
}
