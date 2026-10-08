#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <filesystem>
#include <functional>
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

		// 元のbyte列を変更せずHash化する
		static std::string SHA256(std::span<const uint8_t> bytes);
		// ファイルを分割してHash化する
		static std::string FileSHA256(const std::filesystem::path& path);
		// 分割読込したデータのSHA-256を計算する
		static std::string ReadSHA256(const std::function<bool(std::span<uint8_t>, size_t&)>& read);
	};
}
