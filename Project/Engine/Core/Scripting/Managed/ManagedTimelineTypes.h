#pragma once

//============================================================================
//	include
//============================================================================
#include <cstdint>

namespace Engine {

	struct ManagedTimelineState {

		double time = 0.0;
		double duration = 0.0;
		int32_t playing = 0;
		int32_t paused = 0;
		int32_t finished = 0;
		float preparationProgress = 1.0f;
		int32_t preparing = 0;
		int32_t eventCount = 0;
	};
	struct ManagedTimelineValue {

		int32_t kind = 0;
		int32_t integer = 0;
		float scalar = 0.0f;
		float x = 0.0f, y = 0.0f, z = 0.0f;
	};
	struct ManagedTimelineFrame {

		double time = 0.0;
		double localTime = 0.0;
		double deltaTime = 0.0;
		double duration = 0.0;
		float weight = 1.0f;
		int32_t evaluationMode = 0;
		int32_t phase = 1;
		int32_t exitReason = 0;
		int32_t isScrubbing = 0;
	};
	struct ManagedTimelineBindingUpdate {

		uint64_t id = 0;
		uint32_t worldIndex = 0xFFFFFFFFu, worldGeneration = 0;
		uint32_t index = 0xFFFFFFFFu, generation = 0;
	};
	struct ManagedTimelineParameterUpdate {

		uint64_t id = 0;
		ManagedTimelineValue value{};
		const char* stringValue = nullptr;
	};
	static_assert(sizeof(ManagedTimelineState) == 40);
	static_assert(sizeof(ManagedTimelineValue) == 24);
	static_assert(sizeof(ManagedTimelineFrame) == 56);
	static_assert(sizeof(ManagedTimelineBindingUpdate) == 24);
	static_assert(sizeof(ManagedTimelineParameterUpdate) == 40);
}
