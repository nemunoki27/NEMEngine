#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cstdint>
#include <vector>
// json
#include <json.hpp>

namespace Engine {

	// タイル数をY分割数に合わせて正規化する
	void NormalizeFlipbookTileLayout(std::vector<int32_t>& tilesX, int32_t& tilesY);
	// JSONからタイルレイアウトを読み込む
	void ReadFlipbookTileLayout(const nlohmann::json& in,
		std::vector<int32_t>& tilesX, int32_t& tilesY);
	// JSONへタイルレイアウトを書き出す
	void WriteFlipbookTileLayout(nlohmann::json& out,
		const std::vector<int32_t>& tilesX, int32_t tilesY);
} // Engine
