#include "AssetFileUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <fstream>
#include <iterator>

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

		return Engine::Algorithm::ToLower(
			Engine::Algorithm::PathToUTF8(path.filename())) == "externalactors";
	}
}
