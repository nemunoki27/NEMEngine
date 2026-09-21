#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <filesystem>
// json
#include <json.hpp>

namespace Engine::AssetFileUtility {

	nlohmann::json LoadJsonFileNoThrow(const std::filesystem::path& path);

	bool IsExternalActorsDirectory(const std::filesystem::path& path);
}
