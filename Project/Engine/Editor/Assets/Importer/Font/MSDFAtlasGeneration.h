#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>

namespace Engine::MSDFAtlasGeneration {

	// 指定文字のアトラスと文字配置情報を生成する
	bool Generate(const std::filesystem::path& fontPath, const std::filesystem::path& charsetPath,
		const std::filesystem::path& rawJsonPath, const std::filesystem::path& pngPath);
}
