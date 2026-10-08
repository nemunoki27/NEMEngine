#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>
#include <string>

// json
#include <json.hpp>

namespace Engine::MSDFAtlasGeneration {

	// 保存前の画像と文字配置を所有する
	struct Result {

		nlohmann::json document; // 文字配置情報
		std::string atlasPNG;	 // Atlas画像
	};

	// 指定文字の画像と配置情報をメモリ上で生成する
	bool Generate(const std::filesystem::path& fontPath, const std::filesystem::path& charsetPath, Result& out);
}
