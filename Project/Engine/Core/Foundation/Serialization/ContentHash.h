#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace Engine {

	//============================================================================
	//	ContentHash class
	//	ファイルとメモリのSHA-256を計算するクラス
	//============================================================================
	class ContentHash {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ContentHash() = delete;
		~ContentHash() = delete;

		static std::string SHA256(std::span<const uint8_t> bytes);
		static std::string FileSHA256(const std::filesystem::path& path);
	};
} // Engine
