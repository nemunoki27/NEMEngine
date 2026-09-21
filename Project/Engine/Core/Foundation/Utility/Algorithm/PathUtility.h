#pragma once

//============================================================================
//	include
//============================================================================
#include <filesystem>
#include <string>

namespace Engine::Algorithm {

	std::filesystem::path PathFromUTF8(const std::string& path);

	std::string PathToUTF8(const std::filesystem::path& path);
}
