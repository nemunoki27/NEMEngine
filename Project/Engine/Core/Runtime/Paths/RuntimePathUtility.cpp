#include "RuntimePathResolution.h"

//============================================================================
//	include
//============================================================================

namespace Engine::RuntimePathDetail {

	bool ExistsDirectory(const std::filesystem::path& path) {

		std::error_code ec;
		return std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec);
	}

	std::filesystem::path NormalizePath(const std::filesystem::path& path) {

		std::error_code ec;
		std::filesystem::path result = std::filesystem::weakly_canonical(path, ec);
		if (!ec) {
			return result;
		}
		return path.lexically_normal();
	}

	bool IsChildPath(const std::filesystem::path& relative) {

		if (relative.empty()) {
			return false;
		}
		for (const auto& part : relative) {

			if (part == "..") {
				return false;
			}
		}
		return true;
	}

	std::filesystem::path TryMakeRelative(const std::filesystem::path& fullPath, const std::filesystem::path& root) {

		std::error_code ec;
		std::filesystem::path relative = std::filesystem::relative(fullPath, root, ec);
		if (ec || !IsChildPath(relative)) {
			return {};
		}
		return relative;
	}
}
