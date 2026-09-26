#include "FlipbookFrame.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	FlipbookFrame functions
//============================================================================

Engine::FlipbookFrame Engine::CalcFlipbookFrame(std::span<const int32_t> tilesX, int32_t tilesY, float progress) {

	tilesY = (std::max)(tilesY, 1);
	progress = std::isfinite(progress) ? std::clamp(progress, 0.0f, 1.0f) : 0.0f;

	// 有効な行数
	int32_t rowCount = static_cast<int32_t>((std::min)(static_cast<size_t>(tilesY), tilesX.size()));

	if (rowCount <= 0) {
		return {
			.uvScale = Vector2(1.0f, 1.0f),
			.uvOffset = Vector2(0.0f, 0.0f),
		};
	}

	// 全フレーム数を求める
	uint64_t frameCount = 0;

	for (int32_t y = 0; y < rowCount; ++y) {
		frameCount += static_cast<uint32_t>((std::max)(tilesX[y], 1));
	}

	// 進行度から全体のフレーム番号を求める
	const uint64_t index = progress >= 1.0f ? frameCount - 1 : (std::min)(
		static_cast<uint64_t>(static_cast<double>(progress) * static_cast<double>(frameCount)), frameCount - 1);

	// 全体のフレーム番号から、行と行内の列を求める
	uint64_t remainingIndex = index;
	int32_t rowIndex = 0;
	uint32_t columnIndex = 0;

	for (int32_t y = 0; y < rowCount; ++y) {
		const uint32_t columns =
			static_cast<uint32_t>((std::max)(tilesX[y], 1));

		if (remainingIndex < columns) {
			rowIndex = y;
			columnIndex = static_cast<uint32_t>(remainingIndex);
			break;
		}

		remainingIndex -= columns;
	}

	const int32_t columns = (std::max)(tilesX[rowIndex], 1);

	FlipbookFrame frame{};

	frame.uvScale = Vector2(
		1.0f / static_cast<float>(columns),
		1.0f / static_cast<float>(tilesY)
	);

	frame.uvOffset = Vector2(
		static_cast<float>(columnIndex) * frame.uvScale.x,
		static_cast<float>(rowIndex) * frame.uvScale.y
	);

	return frame;
}