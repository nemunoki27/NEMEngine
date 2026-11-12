#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUState.h"

// c++
#include <algorithm>

namespace Engine {

	// 内部の途中状態復元では、前フレームの子発生先を切り替えない
	inline uint64_t ResolveParticleGPUFeedbackEpoch(const ParticleGPUState& state, uint64_t previous) {

		if (state.suppressFeedback) return 0;
		if (!state.deterministicReplay || !previous) return state.epoch;
		const bool restart = std::any_of(state.pending.begin(), state.pending.end(), [](const auto& step) {
			return step.randomSequence == 0 && !step.suppressFeedback && !step.restoreCheckpoint;
		});
		return restart ? state.epoch : previous;
	}
}
