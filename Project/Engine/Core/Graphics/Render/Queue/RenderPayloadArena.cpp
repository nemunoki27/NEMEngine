#include "RenderPayloadArena.h"

//============================================================================
//	RenderPayloadArena classMethods
//============================================================================

void Engine::RenderPayloadArena::Clear() {

	buffer_.clear();
}

uint32_t Engine::RenderPayloadArena::AlignUp(uint32_t value, uint32_t alignment) {

	if (alignment <= 1u) {
		return value;
	}
	return (value + alignment - 1u) & ~(alignment - 1u);
}