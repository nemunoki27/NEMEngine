#include "FlipbookFrame.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>

//============================================================================
//	FlipbookFrame functions
//============================================================================
Engine::FlipbookFrame Engine::CalcFlipbookFrame(int32_t tilesX, int32_t tilesY, float progress) {

	tilesX = (std::max)(tilesX, 1);
	tilesY = (std::max)(tilesY, 1);
	const int32_t frameCount = tilesX * tilesY;

	FlipbookFrame frame{};
	frame.uvScale = Vector2(1.0f / static_cast<float>(tilesX), 1.0f / static_cast<float>(tilesY));

	const float t = std::clamp(progress, 0.0f, 1.0f);
	const int32_t index = (std::min)(static_cast<int32_t>(t * static_cast<float>(frameCount)), frameCount - 1);
	frame.uvOffset = Vector2(
		static_cast<float>(index % tilesX) * frame.uvScale.x,
		static_cast<float>(index / tilesX) * frame.uvScale.y);
	return frame;
}
