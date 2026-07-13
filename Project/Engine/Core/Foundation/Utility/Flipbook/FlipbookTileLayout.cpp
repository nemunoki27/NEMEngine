#include "FlipbookTileLayout.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <limits>
#include <utility>

//============================================================================
//	FlipbookTileLayout internal
//============================================================================
namespace {

	// JSONの整数を有効なタイル数へ変換する
	bool ReadTileCount(const nlohmann::json& data, int32_t& outValue) {

		if (data.is_number_unsigned()) {
			const uint64_t value = data.get<uint64_t>();
			outValue = static_cast<int32_t>((std::min)(value,
				static_cast<uint64_t>((std::numeric_limits<int32_t>::max)())));
			outValue = (std::max)(outValue, 1);
			return true;
		}
		if (!data.is_number_integer()) {
			return false;
		}
		const int64_t value = data.get<int64_t>();
		outValue = static_cast<int32_t>(std::clamp(value, static_cast<int64_t>(1),
			static_cast<int64_t>((std::numeric_limits<int32_t>::max)())));
		return true;
	}

	// 数値または配列からXタイル数を読み込む
	void ReadTilesX(const nlohmann::json& data, std::vector<int32_t>& outTilesX) {

		int32_t tileCount = 1;
		if (ReadTileCount(data, tileCount)) {
			outTilesX.emplace_back(tileCount);
			return;
		}
		const nlohmann::json* array = &data;
		if (data.is_object()) {

			const auto it = data.find("index");
			if (it == data.end()) {
				return;
			}
			array = &(*it);
		}
		if (!array->is_array()) {
			return;
		}
		for (const nlohmann::json& tile : *array) {

			if (ReadTileCount(tile, tileCount)) {
				outTilesX.emplace_back(tileCount);
			}
		}
	}
}

//============================================================================
//	FlipbookTileLayout functions
//============================================================================
void Engine::NormalizeFlipbookTileLayout(std::vector<int32_t>& tilesX, int32_t& tilesY) {

	tilesY = std::clamp(tilesY, 1, 256);
	for (int32_t& tileX : tilesX) {
		tileX = (std::max)(tileX, 1);
	}
	if (tilesX.empty()) {
		tilesX.resize(static_cast<size_t>(tilesY), 1);
		return;
	}
	const int32_t fillTileX = tilesX.back();
	tilesX.resize(static_cast<size_t>(tilesY), fillTileX);
}

void Engine::ReadFlipbookTileLayout(const nlohmann::json& in,
	std::vector<int32_t>& tilesX, int32_t& tilesY) {

	if (const auto it = in.find("tilesY"); it != in.end()) {
		ReadTileCount(*it, tilesY);
	}
	if (const auto it = in.find("tilesX"); it != in.end()) {

		std::vector<int32_t> loadedTilesX{};
		ReadTilesX(*it, loadedTilesX);
		if (!loadedTilesX.empty()) {
			tilesX = std::move(loadedTilesX);
		}
	}
	NormalizeFlipbookTileLayout(tilesX, tilesY);
}

void Engine::WriteFlipbookTileLayout(nlohmann::json& out,
	const std::vector<int32_t>& tilesX, int32_t tilesY) {

	std::vector<int32_t> normalizedTilesX = tilesX;
	NormalizeFlipbookTileLayout(normalizedTilesX, tilesY);
	out["tilesX"] = normalizedTilesX;
	out["tilesY"] = tilesY;
}
