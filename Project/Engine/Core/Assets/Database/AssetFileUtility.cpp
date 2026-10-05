#include "AssetFileUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <fstream>
#include <iterator>
#include <algorithm>
#include <string_view>

namespace Engine::AssetFileUtility {

	nlohmann::json LoadJsonFileNoThrow(const std::filesystem::path& path) {

		std::ifstream ifs(path, std::ios::binary);
		if (!ifs.is_open()) {
			return nlohmann::json{};
		}
		const std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		return nlohmann::json::parse(content, nullptr, false);
	}

	bool IsExternalActorsDirectory(const std::filesystem::path& path) {

		return Engine::Algorithm::ToLower(Engine::Algorithm::PathToUTF8(path.filename())) == "externalactors";
	}

	bool IsAssetCopyStagingDirectory(const std::filesystem::path& path) {

		// 専用名とGUIDが揃ったディレクトリだけを除外する
		const std::string name = Algorithm::PathToUTF8(path.filename());
		constexpr std::string_view prefix = ".nem-copy-";
		if (!name.starts_with(prefix) || name.size() != prefix.size() + 32) {
			return false;
		}
		return std::all_of(name.begin() + prefix.size(), name.end(), [](char character) {
			return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
				   (character >= 'A' && character <= 'F');
		});
	}
}
