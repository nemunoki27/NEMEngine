#pragma once

#include <filesystem>

namespace Engine::ProductContentWriter {

	bool Write(const std::filesystem::path& stage, const std::filesystem::path& output,
		const std::filesystem::path& manifest, std::string& error);
}
