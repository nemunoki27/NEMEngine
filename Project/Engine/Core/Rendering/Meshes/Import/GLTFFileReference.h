#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>
#include <string>
#include <string_view>

namespace Engine::GLTFFileReference {

	// 外部参照のURIを実ファイルのパスへ変換する
	std::filesystem::path Decode(std::string_view uri);
	// 実ファイルのパスを外部参照のURIへ変換する
	std::string Encode(const std::filesystem::path& path);
}
